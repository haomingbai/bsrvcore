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

#include <algorithm>
#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/post.hpp>
#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/route/http_route_result.h"

namespace {
constexpr auto kAsyncWaitTimeout = std::chrono::seconds(10);
}

TEST(Server, DisableConfigurationWhenRunning) {
  using namespace bsrvcore;

  auto server = std::make_unique<HttpServer>();

  auto start_res = server->Start();
  ASSERT_TRUE(start_res);

  class MyRouteHandler : public HttpRequestHandler {
   public:
    MyRouteHandler() = default;
    void Service(const std::shared_ptr<HttpServerTask>& /*task*/) override {
      return;
    }
  };

  HttpRequestHandler* handler_raw = nullptr;

  auto th = std::thread([&server, &handler_raw] {
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

  th = std::thread([&server, &handler_raw] {
    auto handler = std::make_unique<MyRouteHandler>();
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

  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();
  server.Post([promise] { promise->set_value(true); });

  ASSERT_EQ(future.wait_for(kAsyncWaitTimeout), std::future_status::ready);
  EXPECT_TRUE(future.get());

  server.Stop();
}

TEST(Server, ServiceProviderStoresNonOwningPointer) {
  using namespace bsrvcore;

  HttpServer server(1);

  struct Service {
    int value{0};
  } service{42};

  server.SetServiceProvider(0, &service);

  auto provider = server.GetServiceProvider(0);
  ASSERT_NE(provider.pointer, nullptr);
  ASSERT_NE(provider.Get<Service>(), nullptr);
  EXPECT_EQ(provider.Get<Service>(), &service);
  EXPECT_EQ(provider.Get<Service>()->value, 42);
  EXPECT_EQ(server.GetServiceProvider(1).pointer, nullptr);
}

TEST(Server, GetThreadNativeHandlesExposesRunningThreads) {
  using namespace bsrvcore;

  HttpServer server(1);
  server.AddListen({boost::asio::ip::make_address("127.0.0.1"), 0}, 1);
  ASSERT_TRUE(server.Start());

  auto handles = server.GetThreadNativeHandles();
  EXPECT_FALSE(handles.empty());
  EXPECT_TRUE(std::any_of(handles.begin(), handles.end(),
                          [](const auto& item) { return item.is_control; }));
  EXPECT_TRUE(std::any_of(handles.begin(), handles.end(),
                          [](const auto& item) { return !item.is_control; }));

  server.Stop();
  EXPECT_TRUE(server.GetThreadNativeHandles().empty());
}

TEST(Server, SetTimerDispatchesCallback) {
  using namespace bsrvcore;

  HttpServer server(1);
  ASSERT_TRUE(server.Start());

  auto promise = std::make_shared<std::promise<std::thread::id>>();
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

  auto promise = std::make_shared<std::promise<bool>>();
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
  auto worker_promise = std::make_shared<std::promise<std::thread::id>>();
  auto io_promise = std::make_shared<std::promise<std::thread::id>>();
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

TEST(Server, SupportsStdUniquePtrRegistrationForHandlersAndAspects) {
  using namespace bsrvcore;

  class CountingHandler final : public HttpRequestHandler {
   public:
    explicit CountingHandler(std::atomic<int>* destroyed)
        : destroyed_(destroyed) {}
    ~CountingHandler() override {
      if (destroyed_ != nullptr) {
        destroyed_->fetch_add(1, std::memory_order_relaxed);
      }
    }

    void Service(
        [[maybe_unused]] const std::shared_ptr<HttpServerTask>& task) override {
    }

   private:
    std::atomic<int>* destroyed_;
  };

  class CountingAspect final : public HttpRequestAspectHandler {
   public:
    explicit CountingAspect(std::atomic<int>* destroyed)
        : destroyed_(destroyed) {}
    ~CountingAspect() override {
      if (destroyed_ != nullptr) {
        destroyed_->fetch_add(1, std::memory_order_relaxed);
      }
    }

    void PreService([[maybe_unused]] const std::shared_ptr<HttpPreServerTask>&
                        task) override {}
    void PostService([[maybe_unused]] const std::shared_ptr<HttpPostServerTask>&
                         task) override {}

   private:
    std::atomic<int>* destroyed_;
  };

  std::atomic<int> route_handler_destroyed{0};
  std::atomic<int> default_handler_destroyed{0};
  std::atomic<int> aspect_destroyed{0};

  {
    HttpServer server(1);

    auto route_handler =
        std::make_unique<CountingHandler>(&route_handler_destroyed);
    auto* route_handler_ptr = route_handler.get();
    auto default_handler =
        std::make_unique<CountingHandler>(&default_handler_destroyed);
    auto* default_handler_ptr = default_handler.get();
    auto aspect = std::make_unique<CountingAspect>(&aspect_destroyed);
    auto* aspect_ptr = aspect.get();

    server
        .AddRouteEntry(HttpRequestMethod::kGet, "/users/{id}",
                       std::move(route_handler))
        ->AddTerminalAspect(HttpRequestMethod::kGet, "/users/{id}",
                            std::move(aspect))
        ->SetDefaultHandler(std::move(default_handler));

    const auto matched = server.Route(HttpRequestMethod::kGet, "/users/123");
    EXPECT_EQ(matched.handler, route_handler_ptr);
    ASSERT_EQ(matched.aspects.size(), 1u);
    EXPECT_EQ(matched.aspects[0], aspect_ptr);

    const auto fallback = server.Route(HttpRequestMethod::kGet, "/missing");
    EXPECT_EQ(fallback.handler, default_handler_ptr);
  }

  EXPECT_EQ(route_handler_destroyed.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(default_handler_destroyed.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(aspect_destroyed.load(std::memory_order_relaxed), 1);
}
