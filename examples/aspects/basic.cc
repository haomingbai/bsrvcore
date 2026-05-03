/**
 * @file example_aspect_basic.cc
 * @brief Global, subtree, and terminal aspects.
 *
 * Demonstrates:
 * - AddGlobalAspect for cross-cutting behavior
 * - AddAspect for subtree behavior
 * - AddTerminalAspect for exact-route behavior
 *
 * Prerequisites: Boost, OpenSSL (required by bsrvcore build).
 * Build: cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=ON
 * Run: ./build/examples/example_aspect_basic
 */

// BEGIN README_SNIPPET: aspect_basic

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <iostream>
#include <memory>
#include <string>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/types.h"
#include "bsrvcore/route/http_request_method.h"

int main() {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server
      ->AddGlobalAspect(
          [](const std::shared_ptr<bsrvcore::HttpPreServerTask>& task) {
            task->SetField("X-Request-Start", "1");
          },
          [](const std::shared_ptr<bsrvcore::HttpPostServerTask>& task) {
            task->SetField("X-Request-End", "1");
          })
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/v1/ping",
          [](const std::shared_ptr<bsrvcore::HttpServerTask>& task) {
            task->GetResponse().result(bsrvcore::HttpStatus::ok);
            task->SetField(bsrvcore::HttpField::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("pong");
          })
      ->AddAspect(
          bsrvcore::HttpRequestMethod::kGet, "/v1",
          [](const std::shared_ptr<bsrvcore::HttpPreServerTask>& task) {
            task->SetField("X-Subtree-Aspect", "pre");
          },
          [](const std::shared_ptr<bsrvcore::HttpPostServerTask>& task) {
            task->SetField("X-Subtree-Aspect", "post");
          })
      ->AddTerminalAspect(
          bsrvcore::HttpRequestMethod::kGet, "/v1/ping",
          [](const std::shared_ptr<bsrvcore::HttpPreServerTask>& task) {
            task->SetField("X-Terminal-Aspect", "pre");
          },
          [](const std::shared_ptr<bsrvcore::HttpPostServerTask>& task) {
            task->SetField("X-Terminal-Aspect", "post");
          })
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8083}, 1);

  if (!server->Start()) {
    std::cerr << "Failed to start server." << '\n';
    return 1;
  }

  std::cout << "Listening on http://0.0.0.0:8083/v1/ping" << '\n';
  std::cout << "Press Enter to stop." << '\n';
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: aspect_basic
