/**
 * @file oop_developing.cc
 * @brief Show the OOP mode of developing a server.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-16
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details In software engineering, object oriented programming (OOP) is always
 * a kind of popular way of developing high-quality softwares with a clear
 * structure with the help of plenty of tools like UML diagram and CASE tools.
 * Thus, this framework supports and fully supports this programming mode and
 * the base of this toolkit is OOP. The following example will show how to
 * develop an echo server with OOP programming method.
 */

#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/verb.hpp>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <ostream>

#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"

class EchoHandler : public bsrvcore::HttpRequestHandler {
 public:
  void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
    task->AppendBody(
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>Hello World</title></head>\n");
    if (task->GetRequest().method() == boost::beast::http::verb::post) {
      task->AppendBody(std::format("<body>You POST message is: {}</body>\n",
                                   task->GetRequest().body()));
    } else {
      task->AppendBody("<body>The request methos is not POST.</body>\n");
    }
    task->AppendBody("</html>");
  }
};

int main() {
  auto server = std::make_shared<bsrvcore::HttpServer>(4);
  server
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kPost, "/oop_handler",
                      std::make_unique<EchoHandler>())
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/oop_handler",
                      std::make_unique<EchoHandler>());
  server->AddListen({boost::asio::ip::make_address("0.0.0.0"), 2025});

  if (server->Start(1)) {
    std::cout << "The server starts successfully!" << std::endl;
    getchar();
  } else {
    std::cerr << "The server fails to start." << std::endl;
    std::abort();
  }
}
