#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "test_http_client.h"

namespace {

// Stress test configuration driven by environment variables.
struct StressConfig {
  std::size_t threads;
  std::size_t iterations;
  std::uint64_t seed;
  std::chrono::milliseconds timeout;
};

// Read size values from environment with fallback.
std::size_t GetEnvSize(const char* name, std::size_t fallback) {
  if (const char* val = std::getenv(name)) {
    try {
      return static_cast<std::size_t>(std::stoull(val));
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

// Read uint64 values from environment with fallback.
std::uint64_t GetEnvU64(const char* name, std::uint64_t fallback) {
  if (const char* val = std::getenv(name)) {
    try {
      return static_cast<std::uint64_t>(std::stoull(val));
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

// Load stress config with safe defaults.
StressConfig LoadConfig() {
  StressConfig cfg{};
  cfg.threads = GetEnvSize("BSRVCORE_STRESS_THREADS", 8);
  cfg.iterations = GetEnvSize("BSRVCORE_STRESS_ITERATIONS", 200);
  cfg.seed = GetEnvU64("BSRVCORE_STRESS_SEED", 1337);
  cfg.timeout = std::chrono::milliseconds(
      GetEnvSize("BSRVCORE_STRESS_TIMEOUT_MS", 8000));
  return cfg;
}

}  // namespace

// Run concurrent HTTP requests against the server under load.
TEST(StressHttpServerConcurrencyTest, ConcurrentRequests) {
  const auto cfg = LoadConfig();
  SCOPED_TRACE(::testing::Message()
               << "threads=" << cfg.threads
               << " iterations=" << cfg.iterations << " seed=" << cfg.seed
               << " timeout_ms=" << cfg.timeout.count());

  auto server = std::make_unique<bsrvcore::HttpServer>(cfg.threads);
  server
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/ping",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetBody("pong");
                      })
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kPost, "/echo",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetBody(task->GetRequest().body());
                      });

  bsrvcore::test::ServerGuard guard(std::move(server));
  auto port = bsrvcore::test::StartServerWithRoutes(guard);

  std::atomic<std::size_t> finished{0};
  std::atomic<std::size_t> failures{0};

  std::mutex mtx;
  std::condition_variable cv;
  std::vector<std::string> errors;

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      std::mt19937_64 rng(cfg.seed + t);
      sync.arrive_and_wait();

      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        try {
          if ((rng() & 1U) == 0U) {
            auto res = bsrvcore::test::DoRequestWithRetry(
                bsrvcore::test::http::verb::get, port, "/ping", "");
            if (res.result() != bsrvcore::test::http::status::ok ||
                res.body() != "pong") {
              std::lock_guard<std::mutex> lock(mtx);
              errors.emplace_back("GET /ping unexpected response");
              failures.fetch_add(1, std::memory_order_relaxed);
            }
          } else {
            auto payload = std::to_string(i);
            auto res = bsrvcore::test::DoRequestWithRetry(
                bsrvcore::test::http::verb::post, port, "/echo", payload);
            if (res.result() != bsrvcore::test::http::status::ok ||
                res.body() != payload) {
              std::lock_guard<std::mutex> lock(mtx);
              errors.emplace_back("POST /echo unexpected response");
              failures.fetch_add(1, std::memory_order_relaxed);
            }
          }
        } catch (const std::exception& ex) {
          std::lock_guard<std::mutex> lock(mtx);
          errors.emplace_back(std::string("request failed: ") + ex.what());
          failures.fetch_add(1, std::memory_order_relaxed);
        }
      }

      finished.fetch_add(1, std::memory_order_relaxed);
      cv.notify_one();
    });
  }

  std::unique_lock<std::mutex> lock(mtx);
  bool ok = cv.wait_for(lock, cfg.timeout, [&] {
    return finished.load(std::memory_order_relaxed) == cfg.threads;
  });

  if (!ok) {
    for (auto& th : workers) {
      th.request_stop();
    }
    ADD_FAILURE() << "Timeout waiting for concurrent requests. finished="
                  << finished.load() << "/" << cfg.threads;
  }

  if (!errors.empty()) {
    ADD_FAILURE() << "Encountered " << errors.size()
                  << " request failures; first: " << errors.front();
  }

  EXPECT_EQ(failures.load(), 0u);
}
