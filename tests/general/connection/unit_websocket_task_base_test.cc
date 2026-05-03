#include <gtest/gtest.h>

#include <boost/system/errc.hpp>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/connection/websocket/websocket_task_base.h"

namespace {

struct HandlerState {
  int open_count{0};
  int read_count{0};
  int error_count{0};
  int close_count{0};
  std::string last_payload;
  bool last_binary{false};
  boost::system::error_code last_ec;
  std::string last_error_message;
};

class CountingHandler : public bsrvcore::WebSocketHandler {
 public:
  explicit CountingHandler(std::shared_ptr<HandlerState> state)
      : state_(std::move(state)) {}

  void OnOpen() override { ++state_->open_count; }

  void OnReadMessage(const bsrvcore::WebSocketMessage& message) override {
    ++state_->read_count;
    state_->last_payload = message.payload;
    state_->last_binary = message.binary;
  }

  void OnError(boost::system::error_code ec,
               const std::string& message) override {
    ++state_->error_count;
    state_->last_ec = ec;
    state_->last_error_message = message;
  }

  void OnClose(boost::system::error_code ec) override {
    ++state_->close_count;
    state_->last_ec = ec;
  }

 private:
  std::shared_ptr<HandlerState> state_;
};

class ProbeTask : public bsrvcore::WebSocketTaskBase {
 public:
  explicit ProbeTask(HandlerPtr handler)
      : WebSocketTaskBase(std::move(handler)) {}

  void Start() override {}
  void Cancel() override {}

  bool WriteMessage(std::string, bool) override { return true; }
  bool WriteControl(bsrvcore::WebSocketControlKind, std::string) override {
    return true;
  }

  void EmitOpen() { NotifyOpen(); }
  void EmitMessage(std::string payload, bool binary) {
    bsrvcore::WebSocketMessage message;
    message.payload = std::move(payload);
    message.binary = binary;
    NotifyReadMessage(std::move(message));
  }
  void EmitError(boost::system::error_code ec, std::string message) {
    NotifyError(ec, std::move(message));
  }
  void EmitClose(boost::system::error_code ec) { NotifyClose(ec); }
};

TEST(WebSocketTaskBaseTest, CurrentMessageIsEmptyByDefault) {
  ProbeTask task(nullptr);
  EXPECT_FALSE(task.HasCurrentMessage());
  EXPECT_EQ(task.CurrentMessage(), nullptr);
}

TEST(WebSocketTaskBaseTest, NotifyReadMessageStoresAndForwardsMessage) {
  auto state = std::make_shared<HandlerState>();
  ProbeTask task(std::make_unique<CountingHandler>(state));

  task.EmitMessage("hello", true);

  ASSERT_TRUE(task.HasCurrentMessage());
  ASSERT_NE(task.CurrentMessage(), nullptr);
  EXPECT_EQ(task.CurrentMessage()->payload, "hello");
  EXPECT_TRUE(task.CurrentMessage()->binary);

  EXPECT_EQ(state->read_count, 1);
  EXPECT_EQ(state->last_payload, "hello");
  EXPECT_TRUE(state->last_binary);
}

TEST(WebSocketTaskBaseTest, OpenErrorCloseCallbacksAreForwarded) {
  auto state = std::make_shared<HandlerState>();
  ProbeTask task(std::make_unique<CountingHandler>(state));

  const auto ec = make_error_code(boost::system::errc::operation_canceled);
  task.EmitOpen();
  task.EmitError(ec, "cancelled");
  task.EmitClose(ec);

  EXPECT_EQ(state->open_count, 1);
  EXPECT_EQ(state->error_count, 1);
  EXPECT_EQ(state->close_count, 1);
  EXPECT_EQ(state->last_ec, ec);
  EXPECT_EQ(state->last_error_message, "cancelled");
}

}  // namespace
