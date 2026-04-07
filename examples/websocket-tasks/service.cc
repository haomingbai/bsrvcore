/**
 * @file example_websocket_service.cc
 * @brief WebSocket upgrade endpoint built on HttpServerTask.
 *
 * Demonstrates:
 * - detecting HTTP upgrade requests with IsWebSocketRequest()
 * - upgrading to WebSocket with UpgradeToWebSocket(...)
 * - handling open/read/error/close callbacks on server side
 *
 * Prerequisites: Boost, OpenSSL (required by bsrvcore build).
 * Build: cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=ON
 * Run: ./build/examples/websocket-tasks/example_websocket_service
 */

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <iostream>
#include <memory>
#include <string>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/connection/websocket/websocket_task_base.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"

namespace {

class ExampleServerWebSocketHandler : public bsrvcore::WebSocketHandler {
 public:
  void OnOpen() override { std::cout << "[server] websocket opened" << '\n'; }

  void OnReadMessage(const bsrvcore::WebSocketMessage& message) override {
    std::cout << "[server] received " << (message.binary ? "binary" : "text")
              << " message: " << message.payload << '\n';
  }

  void OnError(boost::system::error_code ec,
               const std::string& message) override {
    std::cerr << "[server] websocket error: " << message << " (" << ec.message()
              << ")" << '\n';
  }

  void OnClose(boost::system::error_code ec) override {
    if (ec) {
      std::cerr << "[server] websocket closed with error: " << ec.message()
                << '\n';
      return;
    }
    std::cout << "[server] websocket closed" << '\n';
  }
};

}  // namespace

int main() {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/ws/tasks",
          [](const std::shared_ptr<bsrvcore::HttpServerTask>& task) {
            if (!task->IsWebSocketRequest()) {
              task->GetResponse().result(bsrvcore::HttpStatus::bad_request);
              task->SetField(bsrvcore::HttpField::content_type,
                             "text/plain; charset=utf-8");
              task->SetBody("Expected a WebSocket upgrade request.");
              return;
            }

            const bool upgraded = task->UpgradeToWebSocket(
                std::make_unique<ExampleServerWebSocketHandler>());
            if (!upgraded) {
              task->GetResponse().result(
                  bsrvcore::HttpStatus::internal_server_error);
              task->SetField(bsrvcore::HttpField::content_type,
                             "text/plain; charset=utf-8");
              task->SetBody("WebSocket upgrade failed.");
            }
          })
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8087}, 1);

  if (!server->Start()) {
    std::cerr << "Failed to start WebSocket server example." << '\n';
    return 1;
  }

  std::cout << "Listening on ws://0.0.0.0:8087/ws/tasks" << '\n';
  std::cout << "Run example_websocket_request in another terminal." << '\n';
  std::cout << "Press Enter to stop." << '\n';
  std::cin.get();

  server->Stop();
  return 0;
}
