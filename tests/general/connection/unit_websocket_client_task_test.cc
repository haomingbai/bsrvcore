#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <memory>
#include <string>

#include "bsrvcore/connection/client/http_client_session.h"
#include "bsrvcore/connection/client/websocket_client_task.h"

namespace {
namespace http = boost::beast::http;

struct ClientHandlerState {
  int open_count{0};
  int error_count{0};
  int close_count{0};
  std::string last_error;
};

class ClientHandler : public bsrvcore::WebSocketHandler {
 public:
  explicit ClientHandler(std::shared_ptr<ClientHandlerState> state)
      : state_(std::move(state)) {}

  void OnOpen() override { ++state_->open_count; }

  void OnReadMessage(const bsrvcore::WebSocketMessage&) override {}

  void OnError(boost::system::error_code, const std::string& message) override {
    ++state_->error_count;
    state_->last_error = message;
  }

  void OnClose(boost::system::error_code) override { ++state_->close_count; }

 private:
  std::shared_ptr<ClientHandlerState> state_;
};

TEST(WebSocketClientTaskTest, CreateHttpSetsUpgradeHeadersOnRequest) {
  boost::asio::io_context ioc;
  auto state = std::make_shared<ClientHandlerState>();
  auto task = bsrvcore::WebSocketClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", "8080", "/ws",
      std::make_unique<ClientHandler>(state));

  ASSERT_NE(task, nullptr);
  auto& request = task->Request();
  EXPECT_EQ(request.method(), http::verb::get);
  EXPECT_EQ(std::string(request[http::field::connection]), "Upgrade");
  EXPECT_EQ(std::string(request[http::field::upgrade]), "websocket");
  EXPECT_EQ(std::string(request[http::field::sec_websocket_version]), "13");
}

TEST(WebSocketClientTaskTest, CreateFromUrlSupportsWsAndWssPrefixes) {
  boost::asio::io_context ioc;
  boost::asio::ssl::context ssl_ctx(boost::asio::ssl::context::tls_client);

  auto ws_task = bsrvcore::WebSocketClientTask::CreateFromUrl(
      ioc.get_executor(), "ws://127.0.0.1:8080/ws",
      std::make_unique<ClientHandler>(std::make_shared<ClientHandlerState>()));
  ASSERT_NE(ws_task, nullptr);

  auto wss_task = bsrvcore::WebSocketClientTask::CreateFromUrl(
      ioc.get_executor(), ssl_ctx, "wss://127.0.0.1:8443/ws",
      std::make_unique<ClientHandler>(std::make_shared<ClientHandlerState>()));
  ASSERT_NE(wss_task, nullptr);

  EXPECT_EQ(ws_task->Request().method(), http::verb::get);
  EXPECT_EQ(wss_task->Request().method(), http::verb::get);
}

TEST(WebSocketClientTaskTest, OnHttpDoneSetterSupportsChaining) {
  boost::asio::io_context ioc;
  auto task = bsrvcore::WebSocketClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", "8080", "/ws",
      std::make_unique<ClientHandler>(std::make_shared<ClientHandlerState>()));
  ASSERT_NE(task, nullptr);

  auto chained = task->OnHttpDone([](const bsrvcore::HttpClientResult&) {});
  EXPECT_EQ(chained.get(), task.get());
}

TEST(WebSocketClientTaskTest, SessionFactoryInjectsCookieBeforeStart) {
  boost::asio::io_context ioc;
  auto session = bsrvcore::HttpClientSession::Create();
  ASSERT_NE(session, nullptr);

  session->SyncSetCookie("127.0.0.1", "/ws", "sid=abc123; Path=/");

  auto task = session->CreateWebSocketHttp(
      ioc.get_executor(), "127.0.0.1", "8080", "/ws",
      std::make_unique<ClientHandler>(std::make_shared<ClientHandlerState>()));
  ASSERT_NE(task, nullptr);

  task->Start();
  const auto cookie = std::string(task->Request()[http::field::cookie]);
  EXPECT_NE(cookie.find("sid=abc123"), std::string::npos);
}

TEST(WebSocketClientTaskTest,
     WriteMethodsReturnFalseBeforeStartAndCancelCloses) {
  boost::asio::io_context ioc;
  auto state = std::make_shared<ClientHandlerState>();
  auto task = bsrvcore::WebSocketClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", "8080", "/ws",
      std::make_unique<ClientHandler>(state));
  ASSERT_NE(task, nullptr);

  EXPECT_FALSE(task->WriteMessage("ping", false));
  EXPECT_FALSE(task->WriteControl(bsrvcore::WebSocketControlKind::kPing));
  EXPECT_EQ(state->error_count, 0);

  task->Cancel();
  EXPECT_EQ(state->close_count, 1);
}

}  // namespace
