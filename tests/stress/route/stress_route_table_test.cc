#include <gtest/gtest.h>

#include <barrier>
#include <memory>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/internal/route/http_route_table.h"
#include "stress_test_common.h"

namespace {

class DummyHandler : public bsrvcore::HttpRequestHandler {
 public:
  explicit DummyHandler(std::string name) : name_(std::move(name)) {}
  void Service(std::shared_ptr<bsrvcore::HttpServerTask>) override {}

  const std::string& Name() const { return name_; }

 private:
  std::string name_;
};

using bsrvcore::test::stress::LoadStressConfig;
using bsrvcore::test::stress::WaitCounter;

TEST(StressRouteTableTest, ConcurrentRouteQueryKeepsExactAndParamRules) {
  const auto cfg = LoadStressConfig(8, 800, 120000);
  SCOPED_TRACE(::testing::Message()
               << "threads=" << cfg.threads << " iterations=" << cfg.iterations
               << " seed=" << cfg.seed
               << " timeout_ms=" << cfg.timeout.count());

  bsrvcore::HttpRouteTable table;
  auto exact_handler = bsrvcore::AllocateUnique<DummyHandler>("exact");
  auto* exact_ptr = exact_handler.get();
  auto param_handler = bsrvcore::AllocateUnique<DummyHandler>("param");
  auto* param_ptr = param_handler.get();

  ASSERT_TRUE(table.AddExclusiveRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/static", std::move(exact_handler)));
  ASSERT_TRUE(table.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet,
                                  "/users/{id}", std::move(param_handler)));

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);
  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      std::mt19937_64 rng(cfg.seed + t);
      sync.arrive_and_wait();

      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        if ((rng() & 1U) == 0U) {
          auto r =
              table.Route(bsrvcore::HttpRequestMethod::kGet, "/static/abc");
          EXPECT_EQ(r.handler, exact_ptr);
        } else {
          auto id = std::to_string(i % 1024U);
          auto r =
              table.Route(bsrvcore::HttpRequestMethod::kGet, "/users/" + id);
          EXPECT_EQ(r.handler, param_ptr);
          ASSERT_EQ(r.parameters.size(), 1u);
          EXPECT_EQ(r.parameters.at("id"), id);
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

TEST(StressRouteTableTest, RouteTableHandlesManyDistinctParamTargets) {
  const auto cfg = LoadStressConfig(8, 500, 120000);

  bsrvcore::HttpRouteTable table;
  auto handler = bsrvcore::AllocateUnique<DummyHandler>("param");
  auto* handler_ptr = handler.get();
  ASSERT_TRUE(table.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet,
                                  "/v/{x}/items/{y}", std::move(handler)));

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);
  std::vector<std::jthread> workers;

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      sync.arrive_and_wait();
      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        auto x = std::to_string(t);
        auto y = std::to_string(i);
        auto r = table.Route(bsrvcore::HttpRequestMethod::kGet,
                             "/v/" + x + "/items/" + y);
        EXPECT_EQ(r.handler, handler_ptr);
        ASSERT_EQ(r.parameters.size(), 2u);
        EXPECT_EQ(r.parameters.at("x"), x);
        EXPECT_EQ(r.parameters.at("y"), y);
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
