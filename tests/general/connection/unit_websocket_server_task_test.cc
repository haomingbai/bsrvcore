#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>

#include "bsrvcore/connection/server/websocket_server_task.h"

namespace {

struct ServerHandlerState {
  int open_count{0};
  int error_count{0};
  int close_count{0};
  std::string last_error;
};

class ServerHandler : public bsrvcore::WebSocketHandler {
 public:
  explicit ServerHandler(std::shared_ptr<ServerHandlerState> state)
      : state_(std::move(state)) {}

  void OnOpen() override { ++state_->open_count; }

  void OnReadMessage(const bsrvcore::WebSocketMessage&) override {}

  void OnError(boost::system::error_code, const std::string& message) override {
    ++state_->error_count;
    state_->last_error = message;
  }

  void OnClose(boost::system::error_code) override { ++state_->close_count; }

 private:
  std::shared_ptr<ServerHandlerState> state_;
};

TEST(WebSocketServerTaskTest, CreateFromNullConnectionReturnsNull) {
  auto state = std::make_shared<ServerHandlerState>();
  bsrvcore::HttpRequest request;
  bsrvcore::HttpResponseHeader response_header;
  auto task = bsrvcore::WebSocketServerTask::CreateFromConnection(
      nullptr, std::move(request), std::move(response_header),
      std::make_unique<ServerHandler>(state));
  EXPECT_EQ(task, nullptr);
}

TEST(WebSocketServerTaskTest, StartWithoutConnectionDoesNotOpen) {
  boost::asio::io_context ioc;
  auto state = std::make_shared<ServerHandlerState>();
  auto task = bsrvcore::WebSocketServerTask::Create(
      ioc.get_executor(), std::make_unique<ServerHandler>(state));

  ASSERT_NE(task, nullptr);
  task->Start();
  task->Start();

  EXPECT_EQ(state->open_count, 0);
}

TEST(WebSocketServerTaskTest, WriteMethodsReturnFalseBeforeUpgrade) {
  boost::asio::io_context ioc;
  auto state = std::make_shared<ServerHandlerState>();
  auto task = bsrvcore::WebSocketServerTask::Create(
      ioc.get_executor(), std::make_unique<ServerHandler>(state));

  ASSERT_NE(task, nullptr);
  EXPECT_FALSE(task->WriteMessage("hello", false));
  EXPECT_FALSE(task->WriteControl(bsrvcore::WebSocketControlKind::kPing));
  EXPECT_EQ(state->error_count, 0);
}

TEST(WebSocketServerTaskTest, CancelIsIdempotentAndFiresOnCloseOnce) {
  boost::asio::io_context ioc;
  auto state = std::make_shared<ServerHandlerState>();
  auto task = bsrvcore::WebSocketServerTask::Create(
      ioc.get_executor(), std::make_unique<ServerHandler>(state));

  ASSERT_NE(task, nullptr);
  task->Cancel();
  task->Cancel();

  EXPECT_EQ(state->close_count, 1);
  EXPECT_FALSE(task->WriteMessage("after-cancel", false));
}

}  // namespace
