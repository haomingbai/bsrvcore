#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "bsrvcore/http_request_aspect_handler.h"
#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/internal/http_route_table.h"

namespace {

// Minimal handler used to verify route selection.
class DummyHandler : public bsrvcore::HttpRequestHandler {
 public:
  void Service(std::shared_ptr<bsrvcore::HttpServerTask>) override {}
};

// Minimal aspect used to verify aspect ordering.
class DummyAspect : public bsrvcore::HttpRequestAspectHandler {
 public:
  void PreService(std::shared_ptr<bsrvcore::HttpServerTask>) override {}
  void PostService(std::shared_ptr<bsrvcore::HttpServerTask>) override {}
};

}  // namespace

// Invalid targets should be rejected by the route table.
TEST(RouteTableTest, RejectsInvalidTarget) {
  bsrvcore::HttpRouteTable table;
  auto ok = table.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "abc",
                                std::make_unique<DummyHandler>());
  EXPECT_FALSE(ok);
}

// Parametric routes should capture parameters correctly.
TEST(RouteTableTest, MatchesParametricRoute) {
  bsrvcore::HttpRouteTable table;
  auto handler = std::make_unique<DummyHandler>();
  auto* handler_ptr = handler.get();

  ASSERT_TRUE(table.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet,
                                  "/users/{id}", std::move(handler)));

  auto result = table.Route(bsrvcore::HttpRequestMethod::kGet, "/users/123");
  EXPECT_EQ(result.handler, handler_ptr);
  ASSERT_EQ(result.parameters.size(), 1u);
  EXPECT_EQ(result.parameters[0], "123");
  EXPECT_EQ(result.current_location, "/users/123");
}

// Exclusive routes should bypass parametric matches.
TEST(RouteTableTest, ExclusiveRouteBypassesParameterRoutes) {
  bsrvcore::HttpRouteTable table;
  auto handler_exclusive = std::make_unique<DummyHandler>();
  auto* exclusive_ptr = handler_exclusive.get();

  auto handler_param = std::make_unique<DummyHandler>();

  ASSERT_TRUE(table.AddExclusiveRouteEntry(bsrvcore::HttpRequestMethod::kGet,
                                           "/static",
                                           std::move(handler_exclusive)));
  ASSERT_TRUE(table.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet,
                                  "/static/{file}",
                                  std::move(handler_param)));

  auto result = table.Route(bsrvcore::HttpRequestMethod::kGet, "/static/abc");
  EXPECT_EQ(result.handler, exclusive_ptr);
}

// Aspect order should be global, method-specific, then route-specific.
TEST(RouteTableTest, AspectOrderIsGlobalMethodThenRoute) {
  bsrvcore::HttpRouteTable table;

  auto handler = std::make_unique<DummyHandler>();
  ASSERT_TRUE(table.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/a",
                                  std::move(handler)));

  auto global = std::make_unique<DummyAspect>();
  auto* global_ptr = global.get();
  ASSERT_TRUE(table.AddGlobalAspect(std::move(global)));

  auto method = std::make_unique<DummyAspect>();
  auto* method_ptr = method.get();
  ASSERT_TRUE(
      table.AddGlobalAspect(bsrvcore::HttpRequestMethod::kGet, std::move(method)));

  auto route = std::make_unique<DummyAspect>();
  auto* route_ptr = route.get();
  ASSERT_TRUE(
      table.AddAspect(bsrvcore::HttpRequestMethod::kGet, "/a", std::move(route)));

  auto result = table.Route(bsrvcore::HttpRequestMethod::kGet, "/a");
  ASSERT_EQ(result.aspects.size(), 3u);
  EXPECT_EQ(result.aspects[0], global_ptr);
  EXPECT_EQ(result.aspects[1], method_ptr);
  EXPECT_EQ(result.aspects[2], route_ptr);
}
