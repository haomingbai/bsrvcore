#include <gtest/gtest.h>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http/field.hpp>
#include <optional>
#include <string>

#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "bsrvcore/internal/http_server_connection.h"

namespace {

// Minimal handler used to construct HttpServerTask in tests.
class DummyHandler : public bsrvcore::HttpRequestHandler {
 public:
  void Service(std::shared_ptr<bsrvcore::HttpServerTask>) override {}
};

// Fake connection captures responses without network I/O.
class FakeConnection : public bsrvcore::HttpServerConnection {
 public:
  FakeConnection(boost::asio::strand<boost::asio::any_io_executor> strand,
                 bsrvcore::HttpServer* server)
      : bsrvcore::HttpServerConnection(std::move(strand), server, 0, 0),
        closed_(false),
        wrote_response_(false) {}

  bool IsStreamAvailable() const noexcept override { return !closed_; }

  void DoWriteResponse(bsrvcore::HttpResponse resp,
                       bool keep_alive) override {
    last_response_ = std::move(resp);
    wrote_response_ = true;
    last_keep_alive_ = keep_alive;
  }

  void DoFlushResponseHeader(
      boost::beast::http::response_header<boost::beast::http::fields>) override {
  }

  void DoFlushResponseBody(std::string) override {}

  void DoClose() override { closed_ = true; }

  bool wrote_response() const { return wrote_response_; }

  const std::optional<bsrvcore::HttpResponse>& last_response() const {
    return last_response_;
  }

  bool last_keep_alive() const { return last_keep_alive_; }

 protected:
  void DoReadHeader() override {}
  void DoReadBody() override {}
  void ClearMessage() override {}

 private:
  bool closed_;
  bool wrote_response_;
  bool last_keep_alive_{false};
  std::optional<bsrvcore::HttpResponse> last_response_;
};

// Build a minimal route result for unit testing tasks.
bsrvcore::HttpRouteResult MakeRouteResult(bsrvcore::HttpRequestHandler* handler) {
  return bsrvcore::HttpRouteResult{
      .current_location = "/",
      .parameters = {},
      .aspects = {},
      .handler = handler,
      .max_body_size = 0,
      .read_expiry = 0,
      .write_expiry = 0,
  };
}

}  // namespace

// When sessionId cookie exists, it should be reused and not reset.
TEST(HttpServerTaskTest, UsesExistingSessionCookie) {
  bsrvcore::HttpServer server(1);
  boost::asio::io_context ioc;
  boost::asio::any_io_executor exec = ioc.get_executor();
  boost::asio::strand<boost::asio::any_io_executor> strand(exec);

  auto conn = std::make_shared<FakeConnection>(std::move(strand), &server);
  DummyHandler handler;

  bsrvcore::HttpRequest req;
  req.set(boost::beast::http::field::cookie, "a=1; sessionId=abc; b=2");

  {
    auto task = std::make_shared<bsrvcore::HttpServerTask>(
        std::move(req), MakeRouteResult(&handler), conn);
    EXPECT_EQ(task->GetCookie("a"), "1");
    EXPECT_EQ(task->GetSessionId(), "abc");
  }

  ASSERT_TRUE(conn->wrote_response());
  ASSERT_TRUE(conn->last_response().has_value());
  EXPECT_EQ(conn->last_response()->base().count(
                boost::beast::http::field::set_cookie),
            0u);
}

// When sessionId cookie is missing, it should be generated and returned.
TEST(HttpServerTaskTest, GeneratesSessionCookieWhenMissing) {
  bsrvcore::HttpServer server(1);
  boost::asio::io_context ioc;
  boost::asio::any_io_executor exec = ioc.get_executor();
  boost::asio::strand<boost::asio::any_io_executor> strand(exec);

  auto conn = std::make_shared<FakeConnection>(std::move(strand), &server);
  DummyHandler handler;

  bsrvcore::HttpRequest req;

  std::string session_id;
  {
    auto task = std::make_shared<bsrvcore::HttpServerTask>(
        std::move(req), MakeRouteResult(&handler), conn);
    session_id = task->GetSessionId();
    EXPECT_FALSE(session_id.empty());
  }

  ASSERT_TRUE(conn->wrote_response());
  ASSERT_TRUE(conn->last_response().has_value());

  auto count = conn->last_response()->base().count(
      boost::beast::http::field::set_cookie);
  EXPECT_GT(count, 0u);

  auto header_view = conn->last_response()->base().at(
      boost::beast::http::field::set_cookie);
  std::string header(header_view);
  EXPECT_NE(header.find("sessionId="), std::string::npos);
  EXPECT_NE(header.find(session_id), std::string::npos);
}

// Manual connection management should skip auto response write on destruction.
TEST(HttpServerTaskTest, ManualConnectionManagementSkipsAutoWrite) {
  bsrvcore::HttpServer server(1);
  boost::asio::io_context ioc;
  boost::asio::any_io_executor exec = ioc.get_executor();
  boost::asio::strand<boost::asio::any_io_executor> strand(exec);

  auto conn = std::make_shared<FakeConnection>(std::move(strand), &server);
  DummyHandler handler;

  bsrvcore::HttpRequest req;

  {
    auto task = std::make_shared<bsrvcore::HttpServerTask>(
        std::move(req), MakeRouteResult(&handler), conn);
    task->SetManualConnectionManagement(true);
  }

  EXPECT_FALSE(conn->wrote_response());
}
