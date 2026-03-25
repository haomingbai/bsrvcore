#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "bsrvcore/blue_print.h"
#include "bsrvcore/http_request_aspect_handler.h"
#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server.h"

namespace {

class DummyHandler : public bsrvcore::HttpRequestHandler {
 public:
  explicit DummyHandler(std::string name) : name_(std::move(name)) {}

  void Service(std::shared_ptr<bsrvcore::HttpServerTask>) override {}

  const std::string& Name() const { return name_; }

 private:
  std::string name_;
};

class DummyAspect : public bsrvcore::HttpRequestAspectHandler {
 public:
  explicit DummyAspect(std::string name) : name_(std::move(name)) {}

  void PreService(std::shared_ptr<bsrvcore::HttpPreServerTask>) override {}
  void PostService(std::shared_ptr<bsrvcore::HttpPostServerTask>) override {}

  const std::string& Name() const { return name_; }

 private:
  std::string name_;
};

}  // namespace

TEST(BluePrintTest, MountsRouteTreeUnderPrefix) {
  bsrvcore::HttpServer server(1);

  auto blue_print = bsrvcore::BluePrintFactory::Create();
  auto handler = bsrvcore::AllocateUnique<DummyHandler>("mounted");
  auto* handler_ptr = handler.get();
  auto aspect = bsrvcore::AllocateUnique<DummyAspect>("route");
  auto* aspect_ptr = aspect.get();

  blue_print
      .AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
                     std::move(handler))
      ->AddAspect(bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
                  std::move(aspect))
      ->SetReadExpiry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}", 1234);

  server.AddBluePrint("/api", std::move(blue_print));

  auto result = server.Route(bsrvcore::HttpRequestMethod::kGet,
                             "/api/users/123");
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
      .AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
                     [](std::shared_ptr<bsrvcore::HttpServerTask>) {})
      ->AddAspect(
          bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
          [](std::shared_ptr<bsrvcore::HttpPreServerTask>) {},
          [](std::shared_ptr<bsrvcore::HttpPostServerTask>) {})
      ->SetWriteExpiry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}", 4321);

  server.AddBluePrint("/v1", blue_print)->AddBluePrint("/v2", blue_print);

  auto v1 =
      server.Route(bsrvcore::HttpRequestMethod::kGet, "/v1/users/alpha");
  auto v2 =
      server.Route(bsrvcore::HttpRequestMethod::kGet, "/v2/users/beta");

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

TEST(BluePrintTest, RejectsConflictingMountWithoutOverwritingExistingRoute) {
  bsrvcore::HttpServer server(1);

  auto existing = bsrvcore::AllocateUnique<DummyHandler>("existing");
  auto* existing_ptr = existing.get();
  ASSERT_NE(existing_ptr, nullptr);
  server.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/api/users/{id}",
                       std::move(existing));

  auto blue_print = bsrvcore::BluePrintFactory::Create();
  auto conflicting = bsrvcore::AllocateUnique<DummyHandler>("conflicting");
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
