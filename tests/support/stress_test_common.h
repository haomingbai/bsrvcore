#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>

namespace bsrvcore::test::stress {

struct StressConfig {
  std::size_t threads;
  std::size_t iterations;
  std::uint64_t seed;
  std::chrono::milliseconds timeout;
};

inline std::size_t GetEnvSize(const char* name, std::size_t fallback) {
  if (const char* val = std::getenv(name)) {
    try {
      return static_cast<std::size_t>(std::stoull(val));
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

inline std::uint64_t GetEnvU64(const char* name, std::uint64_t fallback) {
  if (const char* val = std::getenv(name)) {
    try {
      return static_cast<std::uint64_t>(std::stoull(val));
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

inline StressConfig LoadStressConfig(std::size_t default_threads,
                                     std::size_t default_iterations,
                                     std::size_t default_timeout_ms,
                                     std::uint64_t default_seed = 1337) {
  StressConfig cfg{};
  cfg.threads = GetEnvSize("BSRVCORE_STRESS_THREADS", default_threads);
  cfg.iterations = GetEnvSize("BSRVCORE_STRESS_ITERATIONS", default_iterations);
  cfg.seed = GetEnvU64("BSRVCORE_STRESS_SEED", default_seed);
  cfg.timeout = std::chrono::milliseconds(
      GetEnvSize("BSRVCORE_STRESS_TIMEOUT_MS", default_timeout_ms));
  return cfg;
}

// WaitCounter coordinates completion state across worker threads. The state is:
// (1) workers increment finished counter, (2) main thread waits on cv
// predicate, (3) timeout path reports progress and lets caller stop workers
// deterministically.
class WaitCounter {
 public:
  explicit WaitCounter(std::size_t expected) : expected_(expected) {}

  void MarkOneDone() {
    {
      std::lock_guard<std::mutex> lock(mtx_);
      ++finished_;
    }
    cv_.notify_one();
  }

  bool WaitFor(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mtx_);
    return cv_.wait_for(lock, timeout, [&] { return finished_ >= expected_; });
  }

  std::size_t Finished() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return finished_;
  }

 private:
  std::size_t expected_;
  mutable std::mutex mtx_;
  std::condition_variable cv_;
  std::size_t finished_ = 0;
};

}  // namespace bsrvcore::test::stress
