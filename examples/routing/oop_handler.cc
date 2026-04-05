/**
 * @file example_oop_handler.cc
 * @brief OOP-style request handler with path parameters.
 *
 * Demonstrates:
 * - Custom HttpRequestHandler class
 * - Route parameters via GetPathParameter
 *
 * Prerequisites: Boost, OpenSSL (required by bsrvcore build).
 * Build: cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=ON
 * Run: ./build/examples/example_oop_handler
 */

// BEGIN README_SNIPPET: oop_handler

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <iostream>
#include <memory>
#include <string>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"

class HelloHandler : public bsrvcore::HttpRequestHandler {
 public:
  void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
    const auto* name_param = task->GetPathParameter("name");
    std::string const name = name_param == nullptr ? "world" : *name_param;

    task->GetResponse().result(bsrvcore::HttpStatus::ok);
    task->SetField(bsrvcore::HttpField::content_type,
                   "text/plain; charset=utf-8");
    task->SetBody("Hello, " + name + ".");
  }
};

int main() {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/hello/{name}",
                      bsrvcore::AllocateUnique<HelloHandler>())
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8082}, 1);

  if (!server->Start()) {
    std::cerr << "Failed to start server." << '\n';
    return 1;
  }

  std::cout << "Listening on http://0.0.0.0:8082/hello/{name}" << '\n';
  std::cout << "Press Enter to stop." << '\n';
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: oop_handler
