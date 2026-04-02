/**
 * @file example_aspect_basic.cc
 * @brief Global and route-specific aspects.
 *
 * Demonstrates:
 * - AddGlobalAspect for cross-cutting behavior
 * - AddAspect for route-specific behavior
 *
 * Prerequisites: Boost, OpenSSL (required by bsrvcore build).
 * Build: cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=ON
 * Run: ./build/examples/example_aspect_basic
 */

// BEGIN README_SNIPPET: aspect_basic

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <iostream>
#include <memory>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"

int main() {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server
      ->AddGlobalAspect(
          [](const std::shared_ptr<bsrvcore::HttpPreServerTask>& task) {
            task->SetField("X-Request-Start", "1");
          },
          [](const std::shared_ptr<bsrvcore::HttpPostServerTask>& task) {
            task->SetField("X-Request-End", "1");
          })
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/ping",
          [](const std::shared_ptr<bsrvcore::HttpServerTask>& task) {
            task->GetResponse().result(boost::beast::http::status::ok);
            task->SetField(boost::beast::http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("pong");
          })
      ->AddAspect(
          bsrvcore::HttpRequestMethod::kGet, "/ping",
          [](const std::shared_ptr<bsrvcore::HttpPreServerTask>& task) {
            task->SetField("X-Route-Aspect", "pre");
          },
          [](const std::shared_ptr<bsrvcore::HttpPostServerTask>& task) {
            task->SetField("X-Route-Aspect", "post");
          })
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8083}, 1);

  if (!server->Start()) {
    std::cerr << "Failed to start server." << '\n';
    return 1;
  }

  std::cout << "Listening on http://0.0.0.0:8083/ping" << '\n';
  std::cout << "Press Enter to stop." << '\n';
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: aspect_basic
