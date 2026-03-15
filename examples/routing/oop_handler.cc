/**
 * @file example_oop_handler.cc
 * @brief OOP-style request handler with path parameters.
 *
 * Demonstrates:
 * - Custom HttpRequestHandler class
 * - Route parameters via GetPathParameters
 *
 * Prerequisites: Boost, OpenSSL (required by bsrvcore build).
 * Build: cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=ON
 * Run: ./build/examples/example_oop_handler
 */

// BEGIN README_SNIPPET: oop_handler
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>
#include <string>

class HelloHandler : public bsrvcore::HttpRequestHandler {
 public:
  void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
    const auto& params = task->GetPathParameters();
    std::string name = params.empty() ? "world" : params.front();

    task->GetResponse().result(boost::beast::http::status::ok);
    task->SetField(boost::beast::http::field::content_type,
                   "text/plain; charset=utf-8");
    task->SetBody("Hello, " + name + ".");
  }
};

int main() {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/hello/{name}",
                      std::make_unique<HelloHandler>())
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8082});

  if (!server->Start(1)) {
    std::cerr << "Failed to start server." << std::endl;
    return 1;
  }

  std::cout << "Listening on http://0.0.0.0:8082/hello/{name}" << std::endl;
  std::cout << "Press Enter to stop." << std::endl;
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: oop_handler
