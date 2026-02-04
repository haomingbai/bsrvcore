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
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>

int main() {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server
      ->AddGlobalAspect(
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->SetField("X-Request-Start", "1");
          },
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->SetField("X-Request-End", "1");
          })
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/ping",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->GetResponse().result(boost::beast::http::status::ok);
            task->SetField(boost::beast::http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("pong");
          })
      ->AddAspect(
          bsrvcore::HttpRequestMethod::kGet, "/ping",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->SetField("X-Route-Aspect", "pre");
          },
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->SetField("X-Route-Aspect", "post");
          })
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8083});

  if (!server->Start(1)) {
    std::cerr << "Failed to start server." << std::endl;
    return 1;
  }

  std::cout << "Listening on http://0.0.0.0:8083/ping" << std::endl;
  std::cout << "Press Enter to stop." << std::endl;
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: aspect_basic
