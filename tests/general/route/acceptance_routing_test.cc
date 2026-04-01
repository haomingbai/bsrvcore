/**
 * @file acceptance_routing_test.cc
 * @brief Acceptance tests for public routing behavior.
 *
 * Focus:
 * - invalid route registration should not become reachable
 * - matched tasks should expose stable route metadata
 * - exclusive routes should block parametric fallback
 */

#include <gtest/gtest.h>

#include <boost/beast/core/string.hpp>
#include <string>
#include <string_view>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"
#include "test_http_client_task.h"

namespace {
using bsrvcore::test::DoRequestWithRetry;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;
namespace http = boost::beast::http;

bool HasHeader(const http::response<http::string_body>& response,
               std::string_view name) {
  for (const auto& field : response.base()) {
    if (boost::beast::iequals(field.name_string(), name)) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(RoutingAcceptanceTest, InvalidRoutePatternDoesNotBecomeReachable) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "abc",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetField("X-Invalid-Route-Reached", "1");
                          task->SetBody("unexpected");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto res = DoRequestWithRetry(http::verb::get, port, "/abc", "");
  EXPECT_FALSE(HasHeader(res, "X-Invalid-Route-Reached"));
  EXPECT_NE(res.body(), "unexpected");
}

TEST(RoutingAcceptanceTest, ParametricRouteExposesPublicRouteMetadata) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          const auto* id = task->GetPathParameter("id");
                          ASSERT_NE(id, nullptr);
                          task->SetBody(task->GetCurrentLocation() + "|" +
                                        task->GetRouteTemplate() + "|" + *id);
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto res = DoRequestWithRetry(http::verb::get, port, "/users/123", "");
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "/users/123|/users/{id}|123");
}

TEST(RoutingAcceptanceTest, ExclusiveRouteBypassesParametricRoute) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server
      ->AddExclusiveRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/static",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->SetBody("exclusive");
          })
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/static/{file}",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        const auto* file = task->GetPathParameter("file");
                        task->SetBody(file == nullptr
                                          ? "missing"
                                          : std::string("param:") + *file);
                      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto res = DoRequestWithRetry(http::verb::get, port, "/static/abc", "");
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "exclusive");
}
