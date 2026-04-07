/**
 * @file example_websocket_request.cc
 * @brief WebSocket client request against the websocket service example.
 *
 * Demonstrates:
 * - creating a WebSocketClientTask from ws:// URL
 * - sending one text frame once OnOpen() fires
 * - closing with a WebSocket close control frame
 *
 * Prerequisites: start example_websocket_service first.
 * Build: cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=ON
 * Run: ./build/examples/websocket-tasks/example_websocket_request
 */

#include <atomic>
#include <boost/beast/http/status.hpp>
#include <future>
#include <iostream>
#include <memory>
#include <string>

#include "bsrvcore/connection/client/websocket_client_task.h"

namespace {

struct ClientRunState {
  std::weak_ptr<bsrvcore::WebSocketClientTask> task;
  std::promise<int> exit_promise;
  std::atomic<bool> done{false};
};

void CompleteOnce(const std::shared_ptr<ClientRunState>& state, int code) {
  bool expected = false;
  if (!state->done.compare_exchange_strong(expected, true)) {
    return;
  }
  state->exit_promise.set_value(code);
}

class ExampleClientWebSocketHandler : public bsrvcore::WebSocketHandler {
 public:
  explicit ExampleClientWebSocketHandler(std::shared_ptr<ClientRunState> state)
      : state_(std::move(state)) {}

  void OnOpen() override {
    std::cout << "[client] websocket opened" << '\n';

    auto task = state_->task.lock();
    if (!task) {
      std::cerr << "[client] task handle expired" << '\n';
      CompleteOnce(state_, 1);
      return;
    }

    if (!task->WriteMessage("task:create:demo", false)) {
      std::cerr << "[client] failed to send message" << '\n';
      CompleteOnce(state_, 1);
      return;
    }

    // Close after one request message so this example exits deterministically.
    if (!task->WriteControl(bsrvcore::WebSocketControlKind::kClose)) {
      std::cerr << "[client] failed to send close frame" << '\n';
      CompleteOnce(state_, 1);
    }
  }

  void OnReadMessage(const bsrvcore::WebSocketMessage& message) override {
    std::cout << "[client] received " << (message.binary ? "binary" : "text")
              << " message: " << message.payload << '\n';
  }

  void OnError(boost::system::error_code ec,
               const std::string& message) override {
    std::cerr << "[client] websocket error: " << message << " (" << ec.message()
              << ")" << '\n';
    CompleteOnce(state_, 1);
  }

  void OnClose(boost::system::error_code ec) override {
    if (ec) {
      std::cerr << "[client] websocket closed with error: " << ec.message()
                << '\n';
      CompleteOnce(state_, 1);
      return;
    }

    std::cout << "[client] websocket closed" << '\n';
    CompleteOnce(state_, 0);
  }

 private:
  std::shared_ptr<ClientRunState> state_;
};

}  // namespace

int main() {
  bsrvcore::IoContext ioc;

  auto state = std::make_shared<ClientRunState>();
  auto completion = state->exit_promise.get_future();

  auto task = bsrvcore::WebSocketClientTask::CreateFromUrl(
      ioc.get_executor(), "ws://127.0.0.1:8087/ws/tasks",
      std::make_unique<ExampleClientWebSocketHandler>(state));
  if (!task) {
    std::cerr << "Failed to create WebSocket client task." << '\n';
    return 1;
  }

  state->task = task;
  task->OnHttpDone([](const bsrvcore::HttpClientResult& result) {
    if (result.ec) {
      std::cerr << "[client] handshake failed: " << result.ec.message() << '\n';
      return;
    }

    std::cout << "[client] handshake status: " << result.response.result_int()
              << '\n';
    if (result.response.result() !=
        boost::beast::http::status::switching_protocols) {
      std::cerr << "[client] unexpected handshake response" << '\n';
    }
  });

  task->Start();
  ioc.run();

  return completion.get();
}
