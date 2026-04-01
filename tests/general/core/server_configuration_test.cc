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

#include <atomic>
#include <boost/asio/post.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

  auto start_res = server->Start();
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
  ASSERT_TRUE(server.Start());

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
  ASSERT_TRUE(server.Start());

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
  ASSERT_TRUE(server.Start());

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
  ASSERT_TRUE(server.Start());

  auto caller_id = std::this_thread::get_id();
  auto worker_promise = AllocateShared<std::promise<std::thread::id>>();
  auto io_promise = AllocateShared<std::promise<std::thread::id>>();
  auto worker_future = worker_promise->get_future();
  auto io_future = io_promise->get_future();

  server.Dispatch([worker_promise] {
    worker_promise->set_value(std::this_thread::get_id());
  });
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

TEST(Server, ConcurrentRouteRegistrationBeforeStartIsSafe) {
  using namespace bsrvcore;

  HttpServer server(2);
  constexpr std::size_t kRouteCount = 256;

  std::vector<std::thread> writers;
  writers.reserve(kRouteCount);

  for (std::size_t i = 0; i < kRouteCount; ++i) {
    writers.emplace_back([&server, i] {
      const std::string path = "/route/" + std::to_string(i);
      server.AddRouteEntry(HttpRequestMethod::kGet, path,
                           [i](std::shared_ptr<HttpServerTask> task) {
                             task->SetBody(std::to_string(i));
                           });
    });
  }

  for (auto& th : writers) {
    th.join();
  }

  for (std::size_t i = 0; i < kRouteCount; ++i) {
    const std::string path = "/route/" + std::to_string(i);
    auto route = server.Route(HttpRequestMethod::kGet, path);
    EXPECT_EQ(route.route_template, path);
    EXPECT_NE(route.handler, nullptr);
  }
}

TEST(Server, IoExecutorQueriesRemainSafeWhenStopRaces) {
  using namespace bsrvcore;

  HttpServerRuntimeOptions options;
  options.core_thread_num = 2;
  options.max_thread_num = 2;

  HttpServer server(options);
  ASSERT_TRUE(server.Start());

  std::atomic<bool> keep_running{true};
  std::atomic<std::size_t> observe_count{0};
  std::vector<std::thread> readers;
  readers.reserve(8);

  for (std::size_t i = 0; i < 8; ++i) {
    readers.emplace_back([&] {
      while (keep_running.load(std::memory_order_acquire)) {
        auto exec = server.GetIoExecutor();
        auto endpoint_execs = server.GetEndpointExecutors(0);
        auto global_execs = server.GetGlobalExecutors();
        (void)exec;
        (void)endpoint_execs;
        (void)global_execs;

        server.PostToIoContext([] {});
        server.DispatchToIoContext([] {});

        observe_count.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  server.Stop();
  keep_running.store(false, std::memory_order_release);

  for (auto& th : readers) {
    th.join();
  }

  EXPECT_GT(observe_count.load(std::memory_order_relaxed), 0U);
  EXPECT_TRUE(server.GetGlobalExecutors().empty());
}
