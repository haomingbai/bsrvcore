/**
 * @file example_quick_start.cc
 * @brief Minimal HTTP server with one GET route.
 *
 * Demonstrates:
 * - HttpServer setup and lifecycle
 * - Functional route handler
 *
 * Prerequisites: Boost, OpenSSL (required by bsrvcore build).
 * Build: cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=ON
 * Run: ./build/examples/example_quick_start
 */

// BEGIN README_SNIPPET: quick_start

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
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(4);
  server
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/hello",
          [](const std::shared_ptr<bsrvcore::HttpServerTask>& task) {
            task->GetResponse().result(boost::beast::http::status::ok);
            task->SetField(boost::beast::http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("Hello, bsrvcore.");
          })
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8080}, 2);

  if (!server->Start()) {
    std::cerr << "Failed to start server." << '\n';
    return 1;
  }

  std::cout << "Listening on http://0.0.0.0:8080/hello" << '\n';
  std::cout << "Press Enter to stop." << '\n';
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: quick_start
