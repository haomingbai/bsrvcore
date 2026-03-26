#include <gtest/gtest.h>

#include <barrier>
#include <boost/asio/io_context.hpp>
#include <memory>
#include <mutex>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "bsrvcore/internal/session/session_map.h"
#include "stress_test_common.h"

namespace {

using bsrvcore::test::stress::LoadStressConfig;
using bsrvcore::test::stress::WaitCounter;

TEST(StressSessionMapTest, ConcurrentGetSessionForSameIdReturnsStableContext) {
  const auto cfg = LoadStressConfig(8, 400, 120000);
  SCOPED_TRACE(::testing::Message()
               << "threads=" << cfg.threads << " iterations=" << cfg.iterations
               << " seed=" << cfg.seed
               << " timeout_ms=" << cfg.timeout.count());

  boost::asio::io_context ioc;
  bsrvcore::SessionMap session_map(ioc.get_executor(), nullptr);
  session_map.SetDefaultSessionTimeout(60 * 1000);

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);

  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);

  std::shared_ptr<bsrvcore::Context> baseline;
  std::mutex baseline_mtx;

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      std::mt19937_64 rng(cfg.seed + t);
      sync.arrive_and_wait();

      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        auto ctx = session_map.GetSession("stable-session");
        ASSERT_NE(ctx, nullptr);

        {
          std::lock_guard<std::mutex> lock(baseline_mtx);
          if (!baseline) {
            baseline = ctx;
          }
          EXPECT_EQ(baseline.get(), ctx.get());
        }

        if ((rng() & 7U) == 0U) {
          session_map.SetSessionTimeout("stable-session", 120 * 1000);
        }
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
}

TEST(StressSessionMapTest, ConcurrentCreateAndRemoveSessions) {
  const auto cfg = LoadStressConfig(8, 300, 120000);
  SCOPED_TRACE(::testing::Message()
               << "threads=" << cfg.threads << " iterations=" << cfg.iterations
               << " seed=" << cfg.seed
               << " timeout_ms=" << cfg.timeout.count());

  boost::asio::io_context ioc;
  bsrvcore::SessionMap session_map(ioc.get_executor(), nullptr);
  session_map.SetDefaultSessionTimeout(30 * 1000);

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);

  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      sync.arrive_and_wait();
      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        auto key = "sid-" + std::to_string(t) + "-" + std::to_string(i);
        auto ctx = session_map.GetSession(key);
        EXPECT_NE(ctx, nullptr);

        if ((i % 3U) == 0U) {
          EXPECT_TRUE(session_map.RemoveSession(key));
          auto recreated = session_map.GetSession(key);
          EXPECT_NE(recreated, nullptr);
        }
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
}

}  // namespace
