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
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>

int main() {
  auto server = std::make_unique<bsrvcore::HttpServer>(4);
  server
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/hello",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->GetResponse().result(boost::beast::http::status::ok);
            task->SetField(boost::beast::http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("Hello, bsrvcore.");
          })
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8080});

  if (!server->Start(2)) {
    std::cerr << "Failed to start server." << std::endl;
    return 1;
  }

  std::cout << "Listening on http://0.0.0.0:8080/hello" << std::endl;
  std::cout << "Press Enter to stop." << std::endl;
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: quick_start
