/**
 * @file server_configuration_test.cc
 * @brief HttpServer configuration behavior tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-07
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Verifies configuration calls are rejected/disabled while the server is
 * running.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <boost/asio/post.hpp>
#include <future>
#include <memory>
#include <thread>
#include <utility>

#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"

TEST(Server, DisableConfigurationWhenRunning) {
  using namespace bsrvcore;

  auto server = AllocateUnique<HttpServer>();

  auto start_res = server->Start(1);
  ASSERT_TRUE(start_res);

  class MyRouteHandler : public HttpRequestHandler {
   public:
    MyRouteHandler() = default;
    void Service(std::shared_ptr<HttpServerTask> task) override { return; }
  };

  HttpRequestHandler* handler_raw = nullptr;

  auto th = std::thread([&server, &handler_raw] {
    auto handler = AllocateUnique<MyRouteHandler>();
    handler_raw = handler.get();
    server->SetDefaultHandler(std::move(handler));
  });

  th.join();

  auto route_result_before =
      server->Route(bsrvcore::HttpRequestMethod::kGet, "/");

  ASSERT_NE(route_result_before.handler, handler_raw);

  // --- Test when the server stop.
  server->Stop();

  th = std::thread([&server, &handler_raw] {
    auto handler = AllocateUnique<MyRouteHandler>();
    handler_raw = handler.get();
    server->SetDefaultHandler(std::move(handler));
  });

  th.join();

  auto route_result_after =
      server->Route(bsrvcore::HttpRequestMethod::kGet, "/");

  ASSERT_EQ(route_result_after.handler, handler_raw);
}

TEST(Server, ConstructWithExecutorOptionsAndPost) {
  using namespace bsrvcore;

  HttpServerExecutorOptions options;
  options.core_thread_num = 2;
  options.max_thread_num = 2;
  options.fast_queue_capacity = 128;
  options.thread_clean_interval = 5000;
  options.task_scan_interval = 20;
  options.suspend_time = 1;

  HttpServer server(options);
  ASSERT_TRUE(server.Start(1));

  auto promise = AllocateShared<std::promise<bool>>();
  auto future = promise->get_future();
  server.Post([promise] { promise->set_value(true); });

  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_TRUE(future.get());

  server.Stop();
}

TEST(Server, SetTimerDispatchesCallback) {
  using namespace bsrvcore;

  HttpServer server(1);
  ASSERT_TRUE(server.Start(1));

  auto promise = AllocateShared<std::promise<std::thread::id>>();
  auto future = promise->get_future();
  auto caller_id = std::this_thread::get_id();

  server.SetTimer(10, [promise] { promise->set_value(std::this_thread::get_id()); });

  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_NE(future.get(), caller_id);

  server.Stop();
}

TEST(Server, GetExecutorSupportsAsioPost) {
  using namespace bsrvcore;

  HttpServer server(1);
  ASSERT_TRUE(server.Start(1));

  auto promise = AllocateShared<std::promise<bool>>();
  auto future = promise->get_future();

  boost::asio::post(server.GetExecutor(), [promise] { promise->set_value(true); });

  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_TRUE(future.get());

  server.Stop();
}
