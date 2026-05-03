#include <gtest/gtest.h>

#include <barrier>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <cstddef>
#include <memory>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"
#include "stress_test_common.h"
#include "test_http_client_task.h"

namespace {

namespace http = boost::beast::http;
using bsrvcore::test::DoRequestWithRetry;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;
using bsrvcore::test::stress::LoadStressConfig;
using bsrvcore::test::stress::WaitCounter;

TEST(StressConnectionManagementTest, ConcurrentConnectionEstablishAndClose) {
  const auto cfg = LoadStressConfig(8, 80, 120000);

  auto server = std::make_unique<bsrvcore::HttpServer>(cfg.threads);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/ping",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody("pong");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);

  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      std::mt19937_64 rng(cfg.seed + t);
      sync.arrive_and_wait();

      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        auto res = DoRequestWithRetry(http::verb::get, port, "/ping", "");
        EXPECT_EQ(res.result(), http::status::ok);
        EXPECT_EQ(res.body(), "pong");
        if ((rng() & 15U) == 0U) {
          std::this_thread::yield();
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

TEST(StressConnectionManagementTest, ConcurrentLargePostPayloads) {
  const auto cfg = LoadStressConfig(6, 60, 120000);

  auto server = std::make_unique<bsrvcore::HttpServer>(cfg.threads);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/echo-size",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetBody(std::to_string(task->GetRequest().body().size()));
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);
  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);

  const std::string payload(24 * 1024, 'x');

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      sync.arrive_and_wait();
      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        auto res =
            DoRequestWithRetry(http::verb::post, port, "/echo-size", payload);
        EXPECT_EQ(res.result(), http::status::ok);
        EXPECT_EQ(res.body(), std::to_string(payload.size()));
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
