#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bsrvcore/core/blue_print.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/route/http_route_result.h"

namespace bsrvcore {
class HttpPostServerTask;
class HttpPreServerTask;
class HttpServerTask;
}  // namespace bsrvcore

namespace {

class DummyHandler : public bsrvcore::HttpRequestHandler {
 public:
  explicit DummyHandler(std::string name) : name_(std::move(name)) {}

  void Service(
      const std::shared_ptr<bsrvcore::HttpServerTask>& /*task*/) override {}

  const std::string& Name() const { return name_; }

 private:
  std::string name_;
};

class DummyAspect : public bsrvcore::HttpRequestAspectHandler {
 public:
  explicit DummyAspect(std::string name) : name_(std::move(name)) {}

  void PreService(
      const std::shared_ptr<bsrvcore::HttpPreServerTask>& /*task*/) override {}
  void PostService(
      const std::shared_ptr<bsrvcore::HttpPostServerTask>& /*task*/) override {}

  const std::string& Name() const { return name_; }

 private:
  std::string name_;
};

}  // namespace

TEST(BluePrintTest, MountsRouteTreeUnderPrefix) {
  bsrvcore::HttpServer server(1);

  auto blue_print = bsrvcore::BluePrintFactory::Create();
  auto handler = std::make_unique<DummyHandler>("mounted");
  auto* handler_ptr = handler.get();
  auto aspect = std::make_unique<DummyAspect>("route");
  auto* aspect_ptr = aspect.get();

  blue_print
      .AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
                     std::move(handler))
      ->AddTerminalAspect(bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
                          std::move(aspect))
      ->SetReadExpiry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}", 1234);

  server.AddBluePrint("/api", std::move(blue_print));

  auto result =
      server.Route(bsrvcore::HttpRequestMethod::kGet, "/api/users/123");
  EXPECT_EQ(result.handler, handler_ptr);
  ASSERT_EQ(result.parameters.size(), 1u);
  EXPECT_EQ(result.parameters.at("id"), "123");
  EXPECT_EQ(result.current_location, "/api/users/123");
  EXPECT_EQ(result.route_template, "/api/users/{id}");
  ASSERT_EQ(result.aspects.size(), 1u);
  EXPECT_EQ(result.aspects[0], aspect_ptr);
  EXPECT_EQ(result.read_expiry, 1234u);
}

TEST(BluePrintTest, ReuseableBluePrintClonesOnEachMount) {
  bsrvcore::HttpServer server(1);

  auto blue_print = bsrvcore::BluePrintFactory::CreateReuseable();
  blue_print
      .AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
          [](const std::shared_ptr<bsrvcore::HttpServerTask>& /*task*/) {})
      ->AddTerminalAspect(
          bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
          [](const std::shared_ptr<bsrvcore::HttpPreServerTask>& /*task*/) {},
          [](const std::shared_ptr<bsrvcore::HttpPostServerTask>& /*task*/) {})
      ->SetWriteExpiry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}", 4321);

  server.AddBluePrint("/v1", blue_print)->AddBluePrint("/v2", blue_print);

  auto v1 = server.Route(bsrvcore::HttpRequestMethod::kGet, "/v1/users/alpha");
  auto v2 = server.Route(bsrvcore::HttpRequestMethod::kGet, "/v2/users/beta");

  ASSERT_NE(v1.handler, nullptr);
  ASSERT_NE(v2.handler, nullptr);
  EXPECT_NE(v1.handler, v2.handler);
  ASSERT_EQ(v1.aspects.size(), 1u);
  ASSERT_EQ(v2.aspects.size(), 1u);
  EXPECT_NE(v1.aspects[0], v2.aspects[0]);
  EXPECT_EQ(v1.parameters.at("id"), "alpha");
  EXPECT_EQ(v2.parameters.at("id"), "beta");
  EXPECT_EQ(v1.route_template, "/v1/users/{id}");
  EXPECT_EQ(v2.route_template, "/v2/users/{id}");
  EXPECT_EQ(v1.write_expiry, 4321u);
  EXPECT_EQ(v2.write_expiry, 4321u);
}

TEST(BluePrintTest, MountsSubtreeAspectsUnderPrefixWithoutTerminalFallback) {
  bsrvcore::HttpServer server(1);

  auto blue_print = bsrvcore::BluePrintFactory::Create();
  auto handler = std::make_unique<DummyHandler>("mounted");
  auto* handler_ptr = handler.get();
  auto aspect = std::make_unique<DummyAspect>("subtree");
  auto* aspect_ptr = aspect.get();

  blue_print
      .AddAspect(bsrvcore::HttpRequestMethod::kGet, "/users", std::move(aspect))
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
                      std::move(handler));

  server.AddBluePrint("/api", std::move(blue_print));

  auto child =
      server.Route(bsrvcore::HttpRequestMethod::kGet, "/api/users/123");
  EXPECT_EQ(child.handler, handler_ptr);
  ASSERT_EQ(child.aspects.size(), 1u);
  EXPECT_EQ(child.aspects[0], aspect_ptr);

  auto parent = server.Route(bsrvcore::HttpRequestMethod::kGet, "/api/users");
  EXPECT_NE(parent.handler, handler_ptr);
  EXPECT_TRUE(parent.aspects.empty());
  EXPECT_EQ(parent.route_template, "/");
}

TEST(BluePrintTest, RouteCollectsGlobalMethodSubtreeAndTerminalAspectsInOrder) {
  bsrvcore::HttpServer server(1);

  auto handler = std::make_unique<DummyHandler>("leaf");
  auto* handler_ptr = handler.get();
  auto global_aspect = std::make_unique<DummyAspect>("global");
  auto* global_ptr = global_aspect.get();
  auto method_aspect = std::make_unique<DummyAspect>("method");
  auto* method_ptr = method_aspect.get();
  auto parent_subtree = std::make_unique<DummyAspect>("parent");
  auto* parent_ptr = parent_subtree.get();
  auto leaf_subtree = std::make_unique<DummyAspect>("leaf-subtree");
  auto* leaf_subtree_ptr = leaf_subtree.get();
  auto terminal_aspect = std::make_unique<DummyAspect>("terminal");
  auto* terminal_ptr = terminal_aspect.get();

  server.AddGlobalAspect(std::move(global_aspect))
      ->AddGlobalAspect(bsrvcore::HttpRequestMethod::kGet,
                        std::move(method_aspect))
      ->AddAspect(bsrvcore::HttpRequestMethod::kGet, "/api",
                  std::move(parent_subtree))
      ->AddAspect(bsrvcore::HttpRequestMethod::kGet, "/api/ping",
                  std::move(leaf_subtree))
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/api/ping",
                      std::move(handler))
      ->AddTerminalAspect(bsrvcore::HttpRequestMethod::kGet, "/api/ping",
                          std::move(terminal_aspect));

  auto result = server.Route(bsrvcore::HttpRequestMethod::kGet, "/api/ping");
  EXPECT_EQ(result.handler, handler_ptr);
  ASSERT_EQ(result.aspects.size(), 5u);
  EXPECT_EQ(result.aspects[0], global_ptr);
  EXPECT_EQ(result.aspects[1], method_ptr);
  EXPECT_EQ(result.aspects[2], parent_ptr);
  EXPECT_EQ(result.aspects[3], leaf_subtree_ptr);
  EXPECT_EQ(result.aspects[4], terminal_ptr);
}

TEST(BluePrintTest, InvalidMethodFallsBackToDefaultWithGlobalAspectsOnly) {
  bsrvcore::HttpServer server(1);

  auto default_handler = std::make_unique<DummyHandler>("default");
  auto* default_ptr = default_handler.get();
  auto global_aspect = std::make_unique<DummyAspect>("global");
  auto* global_ptr = global_aspect.get();
  auto method_aspect = std::make_unique<DummyAspect>("method");
  auto* method_ptr = method_aspect.get();

  server.SetDefaultHandler(std::move(default_handler))
      ->AddGlobalAspect(std::move(global_aspect))
      ->AddGlobalAspect(bsrvcore::HttpRequestMethod::kGet,
                        std::move(method_aspect));

  auto result =
      server.Route(static_cast<bsrvcore::HttpRequestMethod>(255), "/somewhere");
  EXPECT_EQ(result.handler, default_ptr);
  EXPECT_EQ(result.current_location, "/");
  EXPECT_EQ(result.route_template, "/");
  ASSERT_EQ(result.aspects.size(), 1u);
  EXPECT_EQ(result.aspects[0], global_ptr);
  EXPECT_NE(result.aspects[0], method_ptr);
}

TEST(BluePrintTest, ExclusiveRouteKeepsMatchedSubtreeAspectsOnDeeperPath) {
  bsrvcore::HttpServer server(1);

  auto subtree_aspect = std::make_unique<DummyAspect>("static");
  auto* subtree_ptr = subtree_aspect.get();
  auto exclusive_handler = std::make_unique<DummyHandler>("static");
  auto* exclusive_ptr = exclusive_handler.get();
  auto param_handler = std::make_unique<DummyHandler>("param");

  server
      .AddAspect(bsrvcore::HttpRequestMethod::kGet, "/static",
                 std::move(subtree_aspect))
      ->AddExclusiveRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/static",
                               std::move(exclusive_handler))
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/static/{file}",
                      std::move(param_handler));

  auto result =
      server.Route(bsrvcore::HttpRequestMethod::kGet, "/static/asset.png");
  EXPECT_EQ(result.handler, exclusive_ptr);
  ASSERT_EQ(result.aspects.size(), 1u);
  EXPECT_EQ(result.aspects[0], subtree_ptr);
  EXPECT_EQ(result.current_location, "/static");
}

TEST(BluePrintTest, RejectsConflictingMountWithoutOverwritingExistingRoute) {
  bsrvcore::HttpServer server(1);

  auto existing = std::make_unique<DummyHandler>("existing");
  auto* existing_ptr = existing.get();
  ASSERT_NE(existing_ptr, nullptr);
  server.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/api/users/{id}",
                       std::move(existing));

  auto blue_print = bsrvcore::BluePrintFactory::Create();
  auto conflicting = std::make_unique<DummyHandler>("conflicting");
  auto* conflicting_ptr = conflicting.get();
  blue_print.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
                           std::move(conflicting));

  server.AddBluePrint("/api", std::move(blue_print));

  auto result =
      server.Route(bsrvcore::HttpRequestMethod::kGet, "/api/users/123");
  EXPECT_EQ(result.handler, existing_ptr);
  EXPECT_NE(result.handler, conflicting_ptr);
  EXPECT_EQ(result.route_template, "/api/users/{id}");
}
