#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/websocket.hpp>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "bsrvcore/connection/client/http_client_session.h"
#include "bsrvcore/connection/client/websocket_client_task.h"

namespace {
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;

struct IntegrationHandlerState {
  int open_count{0};
  int error_count{0};
  int close_count{0};
  std::string last_error;
};

class IntegrationHandler : public bsrvcore::WebSocketHandler {
 public:
  explicit IntegrationHandler(std::shared_ptr<IntegrationHandlerState> state)
      : state_(std::move(state)) {}

  void OnOpen() override { ++state_->open_count; }
  void OnReadMessage(const bsrvcore::WebSocketMessage&) override {}
  void OnError(boost::system::error_code, const std::string& message) override {
    ++state_->error_count;
    state_->last_error = message;
  }
  void OnClose(boost::system::error_code) override { ++state_->close_count; }

 private:
  std::shared_ptr<IntegrationHandlerState> state_;
};

TEST(WebSocketIntegrationTest, ClientTaskOpensWhenHandshakeSucceeds) {
  boost::asio::io_context server_ioc;
  tcp::acceptor acceptor(server_ioc, tcp::endpoint(tcp::v4(), 0));
  const auto port = acceptor.local_endpoint().port();
  std::thread server_thread([&acceptor]() {
    tcp::socket socket(acceptor.get_executor());
    boost::system::error_code ec;
    acceptor.accept(socket, ec);
    if (ec) {
      return;
    }

    websocket::stream<tcp::socket> ws(std::move(socket));
    ws.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& response) {
          response.set(http::field::set_cookie, "sid=ws-session; Path=/ws");
        }));
    ws.accept(ec);
    if (ec) {
      return;
    }

    ws.close(websocket::close_code::normal, ec);
  });

  boost::asio::io_context ioc;
  auto session = bsrvcore::HttpClientSession::Create();
  ASSERT_NE(session, nullptr);

  auto state = std::make_shared<IntegrationHandlerState>();
  const auto cookie_count_before = session->CookieCount();
  auto task = session->CreateWebSocketHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), "/ws",
      std::make_unique<IntegrationHandler>(state));

  std::promise<bsrvcore::HttpClientResult> done_promise;
  auto done_future = done_promise.get_future();
  task->OnHttpDone([&done_promise](const bsrvcore::HttpClientResult& result) {
    done_promise.set_value(result);
  });

  task->Start();
  ioc.run();
  server_thread.join();

  const auto done = done_future.get();
  EXPECT_FALSE(done.ec);
  EXPECT_EQ(done.response.result(), http::status::switching_protocols);
  EXPECT_EQ(state->open_count, 1);
  EXPECT_GT(session->CookieCount(), cookie_count_before);
}

TEST(WebSocketIntegrationTest, ClientTaskReportsErrorForNonWebSocketResponse) {
  boost::asio::io_context server_ioc;
  tcp::acceptor acceptor(server_ioc, tcp::endpoint(tcp::v4(), 0));
  const auto port = acceptor.local_endpoint().port();
  std::thread server_thread([&acceptor]() {
    tcp::socket socket(acceptor.get_executor());
    boost::system::error_code ec;
    acceptor.accept(socket, ec);
    if (ec) {
      return;
    }

    static constexpr char kResponse[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n"
        "\r\n"
        "ok";
    boost::asio::write(socket, boost::asio::buffer(kResponse), ec);
  });

  boost::asio::io_context ioc;
  auto state = std::make_shared<IntegrationHandlerState>();
  auto task = bsrvcore::WebSocketClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), "/",
      std::make_unique<IntegrationHandler>(state));

  task->Start();
  ioc.run();
  server_thread.join();

  EXPECT_EQ(state->open_count, 0);
  EXPECT_EQ(state->error_count, 1);
  EXPECT_EQ(state->close_count, 1);
  EXPECT_NE(state->last_error.find("handshake"), std::string::npos);
}

}  // namespace
