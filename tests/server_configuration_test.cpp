/**
 * @file test_dummy.cpp
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-07
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <utility>

#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"

TEST(Server, DisableConfigurationWhenRunning) {
  using namespace bsrvcore;

  auto server = new HttpServer();

  auto start_res = server->Start(1);
  ASSERT_EQ(start_res, 1);

  class MyRouteHandler : public HttpRequestHandler {
   public:
    MyRouteHandler() = default;
    void Service(std::shared_ptr<HttpServerTask> task) override { return; }
  };

  HttpRequestHandler* handler_raw;

  auto th = std::thread([server, &handler_raw] {
    auto handler = std::make_unique<MyRouteHandler>();
    handler_raw = handler.get();
    server->SetDefaultHandler(std::move(handler));
  });

  th.join();

  auto route_result_before =
      server->Route(bsrvcore::HttpRequestMethod::kGet, "/");

  ASSERT_NE(route_result_before.handler, handler_raw);

  // --- Test when the server stop.
  server->Stop();

  th = std::thread([server, &handler_raw] {
    auto handler = std::make_unique<MyRouteHandler>();
    handler_raw = handler.get();
    server->SetDefaultHandler(std::move(handler));
  });

  th.join();

  auto route_result_after =
      server->Route(bsrvcore::HttpRequestMethod::kGet, "/");

  ASSERT_EQ(route_result_after.handler, handler_raw);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
