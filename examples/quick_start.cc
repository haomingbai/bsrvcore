/**
 * @file quick_start.cpp
 * @brief A quick RESTful HTTP server.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-16
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

// # You may use the following command to test:
// # Test the get handler.
// curl http://localhost:2025/hello/get
// # Test the post handler.
// curl -X POST --data "Hello HTTP server." http://localhost:2025/hello/post #

#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <cassert>
#include <cstdio>
#include <format>
#include <memory>

int main() {
  using namespace bsrvcore;

  // Notes: In real practice, please use RAII to manage resources.
  HttpServer* server = new HttpServer(10);
  server
      // Add an entry to process a GET request.
      ->AddRouteEntry(
          HttpRequestMethod::kGet, "/hello/get",
          [](std::shared_ptr<HttpServerTask> task) {
            task->SetBody(
                "<!DOCTYPE html><title>Hello World in GET method.</title>");
          })
      // An echo handler to process the POST request.
      ->AddRouteEntry(HttpRequestMethod::kPost, "/hello/post",
                      [](std::shared_ptr<HttpServerTask> task) {
                        const auto& request_body = task->GetRequest().body();
                        task->SetBody(
                            "<!DOCTYPE html>\n"
                            "<html>\n"
                            "<head><title>Hello World</title></head>\n");
                        auto formated_string = std::format(
                            "<body>You request body is: {}</body>\n",
                            request_body);
                        task->AppendBody(formated_string);
                        task->AppendBody("</html>");
                      })
      // Listen to 0.0.0.0:2025
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 2025});

  // Start the server.
  if (server->Start(2)) {
    getchar();
    // Wait the server to stop and clean resources.
    server->Stop();
    delete server;
  } else {
    delete server;
    assert(0 && "The server fails to start.");
  }
}
