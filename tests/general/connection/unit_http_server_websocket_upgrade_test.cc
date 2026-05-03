#include <gtest/gtest.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/errc.hpp>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/connection/websocket/websocket_task_base.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"
#include "test_http_client_task.h"

namespace {
namespace http = boost::beast::http;
using bsrvcore::test::DoRequestWithRetry;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;

class NoopWebSocketHandler : public bsrvcore::WebSocketHandler {
 public:
  void OnReadMessage(const bsrvcore::WebSocketMessage&) override {}
  void OnError(boost::system::error_code, const std::string&) override {}
  void OnClose(boost::system::error_code) override {}
};

TEST(HttpServerWebSocketUpgradeTest, IsWebSocketRequestFalseForNormalHttp) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/check",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetBody(task->IsWebSocketRequest() ? "true" : "false");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto response = DoRequestWithRetry(http::verb::get, port, "/check", "");
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), "false");
}

TEST(HttpServerWebSocketUpgradeTest, IsWebSocketRequestTrueForUpgradeHeaders) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/check",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetBody(task->IsWebSocketRequest() ? "true" : "false");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto response =
      DoRequestWithRetry(http::verb::get, port, "/check", "",
                         [](http::request<http::string_body>& request) {
                           request.set(http::field::connection, "Upgrade");
                           request.set(http::field::upgrade, "websocket");
                         });

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), "true");
}

TEST(HttpServerWebSocketUpgradeTest,
     UpgradeToWebSocketReturnsFalseWhenRequestIsNotUpgrade) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/upgrade",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          EXPECT_FALSE(task->IsWebSocketUpgradeMarked());
                          const bool upgraded = task->UpgradeToWebSocket(
                              std::make_unique<NoopWebSocketHandler>());
                          EXPECT_FALSE(task->IsWebSocketUpgradeMarked());
                          task->SetBody(upgraded ? "ok" : "null");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto response =
      DoRequestWithRetry(http::verb::get, port, "/upgrade", "");
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), "null");
}

}  // namespace
