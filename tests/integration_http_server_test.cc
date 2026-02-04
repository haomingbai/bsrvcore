#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "test_http_client.h"

namespace {
using bsrvcore::test::DoRequestWithRetry;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;
namespace http = boost::beast::http;

}  // namespace

// Verify basic GET/POST handling end-to-end.
TEST(HttpServerIntegrationTest, BasicGetAndPost) {
  auto server = std::make_unique<bsrvcore::HttpServer>(4);
  server
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/ping",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetBody("pong");
                      })
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kPost, "/echo",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetBody(task->GetRequest().body());
                      });

  ServerGuard guard(std::move(server));
  auto port = StartServerWithRoutes(guard);

  auto get_res = DoRequestWithRetry(http::verb::get, port, "/ping", "");
  EXPECT_EQ(get_res.result(), http::status::ok);
  EXPECT_EQ(get_res.body(), "pong");

  auto post_res = DoRequestWithRetry(http::verb::post, port, "/echo",
                                     "hello");
  EXPECT_EQ(post_res.result(), http::status::ok);
  EXPECT_EQ(post_res.body(), "hello");
}

// Verify aspect order across global/method/route hooks.
TEST(HttpServerIntegrationTest, AspectOrderIsDeterministic) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);

  server
      ->AddGlobalAspect(
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->AppendBody("preG|");
          },
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->AppendBody("postG|");
          })
      ->AddGlobalAspect(
          bsrvcore::HttpRequestMethod::kGet,
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->AppendBody("preM|");
          },
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->AppendBody("postM|");
          })
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/order",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->AppendBody("handler|");
          })
      ->AddAspect(
          bsrvcore::HttpRequestMethod::kGet, "/order",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->AppendBody("preR|");
          },
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->AppendBody("postR|");
          });

  ServerGuard guard(std::move(server));
  auto port = StartServerWithRoutes(guard);

  auto res = DoRequestWithRetry(http::verb::get, port, "/order", "");
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "preG|preM|preR|handler|postR|postM|postG|");
}
