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

#include <boost/asio/post.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <utility>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"

namespace {
constexpr auto kAsyncWaitTimeout = std::chrono::seconds(10);
}

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

  HttpServer server(options);
  ASSERT_TRUE(server.Start(1));

  auto promise = AllocateShared<std::promise<bool>>();
  auto future = promise->get_future();
  server.Post([promise] { promise->set_value(true); });

  ASSERT_EQ(future.wait_for(kAsyncWaitTimeout), std::future_status::ready);
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

  // Keep a modest delay to reduce scheduler-jitter flakes on busy CI runners.
  server.SetTimer(
      50, [promise] { promise->set_value(std::this_thread::get_id()); });

  ASSERT_EQ(future.wait_for(kAsyncWaitTimeout), std::future_status::ready);
  EXPECT_NE(future.get(), caller_id);

  server.Stop();
}

TEST(Server, GetExecutorSupportsAsioPost) {
  using namespace bsrvcore;

  HttpServer server(1);
  ASSERT_TRUE(server.Start(1));

  auto promise = AllocateShared<std::promise<bool>>();
  auto future = promise->get_future();

  boost::asio::post(server.GetExecutor(),
                    [promise] { promise->set_value(true); });

  ASSERT_EQ(future.wait_for(kAsyncWaitTimeout), std::future_status::ready);
  EXPECT_TRUE(future.get());

  server.Stop();
}

TEST(Server, DispatchAndIoContextHelpersTargetExpectedExecutors) {
  using namespace bsrvcore;

  HttpServerRuntimeOptions options;
  options.core_thread_num = 1;
  options.max_thread_num = 1;

  HttpServer server(options);
  ASSERT_TRUE(server.Start(1));

  auto caller_id = std::this_thread::get_id();
  auto worker_promise = AllocateShared<std::promise<std::thread::id>>();
  auto io_promise = AllocateShared<std::promise<std::thread::id>>();
  auto worker_future = worker_promise->get_future();
  auto io_future = io_promise->get_future();

  server.Dispatch(
      [worker_promise] { worker_promise->set_value(std::this_thread::get_id()); });
  server.DispatchToIoContext(
      [io_promise] { io_promise->set_value(std::this_thread::get_id()); });

  ASSERT_EQ(worker_future.wait_for(kAsyncWaitTimeout),
            std::future_status::ready);
  ASSERT_EQ(io_future.wait_for(kAsyncWaitTimeout), std::future_status::ready);

  const auto worker_id = worker_future.get();
  const auto io_id = io_future.get();
  EXPECT_NE(worker_id, caller_id);
  EXPECT_NE(io_id, caller_id);
  EXPECT_NE(worker_id, io_id);

  server.Stop();
}
