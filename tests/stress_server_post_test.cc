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
#include <thread>
#include <vector>

#include "bsrvcore/http_server.h"

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
  cfg.iterations = GetEnvSize("BSRVCORE_STRESS_ITERATIONS", 5000);
  cfg.seed = GetEnvU64("BSRVCORE_STRESS_SEED", 1337);
  cfg.timeout = std::chrono::milliseconds(
      GetEnvSize("BSRVCORE_STRESS_TIMEOUT_MS", 5000));
  return cfg;
}

}  // namespace

// Flood HttpServer::Post with concurrent producers and verify completion.
TEST(StressServerPostTest, FloodPostTasks) {
  const auto cfg = LoadConfig();
  SCOPED_TRACE(::testing::Message()
               << "threads=" << cfg.threads
               << " iterations=" << cfg.iterations << " seed=" << cfg.seed
               << " timeout_ms=" << cfg.timeout.count());

  bsrvcore::HttpServer server(cfg.threads);
  ASSERT_TRUE(server.Start(1));

  const std::size_t total = cfg.threads * cfg.iterations;
  std::atomic<std::size_t> executed{0};
  std::atomic<std::uint64_t> checksum{0};
  std::atomic<std::uint64_t> expected{0};

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  std::mutex mtx;
  std::condition_variable cv;

  std::vector<std::jthread> producers;
  producers.reserve(cfg.threads);

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    producers.emplace_back([&, t](std::stop_token st) {
      std::mt19937_64 rng(cfg.seed + t);
      std::uint64_t local_expected = 0;

      sync.arrive_and_wait();

      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        auto payload = rng();
        local_expected += payload;
        server.Post([&executed, &checksum, payload, total, &cv, &mtx] {
          checksum.fetch_add(payload, std::memory_order_relaxed);
          auto count = executed.fetch_add(1, std::memory_order_relaxed) + 1;
          if (count == total) {
            std::lock_guard<std::mutex> lock(mtx);
            cv.notify_one();
          }
        });
      }

      expected.fetch_add(local_expected, std::memory_order_relaxed);
    });
  }

  std::unique_lock<std::mutex> lock(mtx);
  bool ok = cv.wait_for(lock, cfg.timeout, [&] {
    return executed.load(std::memory_order_relaxed) >= total;
  });

  if (!ok) {
    for (auto& th : producers) {
      th.request_stop();
    }
    ADD_FAILURE() << "Timeout waiting for posted tasks. executed="
                  << executed.load() << "/" << total;
  }

  EXPECT_EQ(executed.load(), total);
  EXPECT_EQ(checksum.load(), expected.load());

  server.Stop();
}
