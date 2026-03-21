#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "stress_test_common.h"
#include "test_http_client_task.h"

namespace {

namespace http = boost::beast::http;
using bsrvcore::test::DoRequestWithRetry;
using bsrvcore::test::FindFreePort;
using bsrvcore::test::stress::LoadStressConfig;
using bsrvcore::test::stress::WaitCounter;

TEST(StressServerRuntimeTest, CyclicServerStartStop) {
  const auto cfg = LoadStressConfig(4, 8, 120000);
  const std::size_t cycles = std::min<std::size_t>(cfg.iterations, 10);

  for (std::size_t i = 0; i < cycles; ++i) {
    bsrvcore::HttpServer server(cfg.threads);
    server.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/ping",
                         [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                           task->SetBody("pong");
                         });
    const auto port = FindFreePort();
    server.AddListen({boost::asio::ip::make_address("127.0.0.1"), port});
    ASSERT_TRUE(server.Start(1));

    auto res = DoRequestWithRetry(http::verb::get, port, "/ping", "");
    EXPECT_EQ(res.result(), http::status::ok);
    EXPECT_EQ(res.body(), "pong");

    server.Stop();
  }
}

TEST(StressServerRuntimeTest, MultipleListenersUnderConcurrentLoad) {
  const auto cfg = LoadStressConfig(6, 60, 120000);

  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(cfg.threads);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/ping",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody("pong");
                        });

  std::vector<unsigned short> ports;
  for (int i = 0; i < 3; ++i) {
    auto port = FindFreePort();
    ports.push_back(port);
    server->AddListen({boost::asio::ip::make_address("127.0.0.1"), port});
  }

  ASSERT_TRUE(server->Start(1));

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);
  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      std::mt19937_64 rng(cfg.seed + t);
      sync.arrive_and_wait();

      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        auto port = ports[rng() % ports.size()];
        auto res = DoRequestWithRetry(http::verb::get, port, "/ping", "");
        EXPECT_EQ(res.result(), http::status::ok);
        EXPECT_EQ(res.body(), "pong");
      }
      done.MarkOneDone();
    });
  }

  const bool completed = done.WaitFor(cfg.timeout);
  if (!completed) {
    for (auto& th : workers) {
      th.request_stop();
    }
    ADD_FAILURE() << "Timeout waiting for workers. finished=" << done.Finished()
                  << "/" << cfg.threads;
  }

  server->Stop();
}

TEST(StressServerRuntimeTest, PostQueueCompletesAllTasks) {
  const auto cfg = LoadStressConfig(6, 120, 120000);

  bsrvcore::HttpServer server(cfg.threads);
  ASSERT_TRUE(server.Start(1));

  const std::size_t total = cfg.threads * cfg.iterations;
  std::atomic<std::size_t> executed{0};
  WaitCounter done(1);

  std::vector<std::jthread> producers;
  producers.reserve(cfg.threads);

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    producers.emplace_back([&, t](std::stop_token st) {
      std::mt19937_64 rng(cfg.seed + t);
      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        auto payload = rng();
        server.Post([&executed, total, &done, payload]() {
          (void)payload;
          auto count = executed.fetch_add(1, std::memory_order_relaxed) + 1;
          if (count >= total) {
            done.MarkOneDone();
          }
        });
      }
    });
  }

  const bool completed = done.WaitFor(cfg.timeout);
  if (!completed) {
    for (auto& th : producers) {
      th.request_stop();
    }
    ADD_FAILURE() << "Timeout waiting for posted tasks. executed="
                  << executed.load(std::memory_order_relaxed) << "/" << total;
  }

  for (auto& th : producers) {
    th.join();
  }

  EXPECT_EQ(executed.load(std::memory_order_relaxed), total);
  server.Stop();
}

}  // namespace
