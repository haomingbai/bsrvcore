#include <gtest/gtest.h>

#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "bsrvcore/attribute.h"
#include "bsrvcore/context.h"

namespace {

// Lightweight attribute used in stress writes.
class IntAttribute : public bsrvcore::CloneableAttribute<IntAttribute> {
 public:
  explicit IntAttribute(int v) : value(v) {}
  int value;
};

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

// Run concurrent Context access under load with timeouts.
TEST(StressContextTest, ConcurrentSetGet) {
  const auto cfg = LoadConfig();
  SCOPED_TRACE(::testing::Message()
               << "threads=" << cfg.threads
               << " iterations=" << cfg.iterations << " seed=" << cfg.seed
               << " timeout_ms=" << cfg.timeout.count());

  bsrvcore::Context ctx;
  constexpr int kKeys = 128;
  for (int i = 0; i < kKeys; ++i) {
    ctx.SetAttribute("k" + std::to_string(i),
                     std::make_shared<IntAttribute>(i));
  }

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  std::mutex mtx;
  std::condition_variable cv;
  std::size_t finished = 0;

  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      std::mt19937_64 rng(cfg.seed + t);
      sync.arrive_and_wait();

      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        int idx = static_cast<int>(rng() % kKeys);
        auto key = "k" + std::to_string(idx);
        ctx.SetAttribute(key, std::make_shared<IntAttribute>(idx + 1));
        auto got = ctx.GetAttribute(key);
        if (!got) {
          ADD_FAILURE() << "Missing attribute for key=" << key;
          break;
        }
      }

      {
        std::lock_guard<std::mutex> lock(mtx);
        finished++;
      }
      cv.notify_one();
    });
  }

  std::unique_lock<std::mutex> lock(mtx);
  bool ok = cv.wait_for(lock, cfg.timeout, [&] {
    return finished == cfg.threads;
  });

  if (!ok) {
    for (auto& th : workers) {
      th.request_stop();
    }
    ADD_FAILURE() << "Timeout waiting for stress threads. finished="
                  << finished << "/" << cfg.threads;
  }

  for (int i = 0; i < kKeys; ++i) {
    auto key = "k" + std::to_string(i);
    EXPECT_TRUE(ctx.HasAttribute(key));
    EXPECT_NE(ctx.GetAttribute(key), nullptr);
  }
}
