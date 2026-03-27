#include <gtest/gtest.h>

#include <barrier>
#include <mutex>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"
#include "stress_test_common.h"
#include "test_http_client_task.h"

namespace {
using bsrvcore::test::stress::LoadStressConfig;
using bsrvcore::test::stress::WaitCounter;
namespace http = boost::beast::http;

TEST(StressRoutingAcceptanceTest,
     ConcurrentRequestsKeepExclusiveAndParamRules) {
  const auto cfg = LoadStressConfig(6, 80, 120000);
  SCOPED_TRACE(::testing::Message()
               << "threads=" << cfg.threads << " iterations=" << cfg.iterations
               << " seed=" << cfg.seed
               << " timeout_ms=" << cfg.timeout.count());

  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(cfg.threads);
  server
      ->AddExclusiveRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/static",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->SetBody("exclusive");
          })
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        const auto* id = task->GetPathParameter("id");
                        if (id == nullptr) {
                          task->GetResponse().result(http::status::bad_request);
                          task->SetBody("missing-id");
                          return;
                        }
                        task->SetBody(*id);
                      });

  bsrvcore::test::ServerGuard guard(std::move(server));
  const auto port = bsrvcore::test::FindFreePort();
  guard.server->AddListen({boost::asio::ip::make_address("127.0.0.1"), port});
  ASSERT_TRUE(guard.server->Start(cfg.threads));

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);
  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);
  std::mutex error_mutex;
  std::vector<std::string> errors;

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      std::mt19937_64 rng(cfg.seed + t);
      sync.arrive_and_wait();

      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        try {
          if ((rng() & 1U) == 0U) {
            const auto response = bsrvcore::test::DoRequestWithRetry(
                http::verb::get, port, "/static/abc", "");
            if (response.result() != http::status::ok ||
                response.body() != "exclusive") {
              std::lock_guard<std::mutex> lock(error_mutex);
              errors.emplace_back("exclusive route returned unexpected body");
            }
          } else {
            const auto id = std::to_string(i % 1024U);
            const auto response = bsrvcore::test::DoRequestWithRetry(
                http::verb::get, port, "/users/" + id, "");
            if (response.result() != http::status::ok ||
                response.body() != id) {
              std::lock_guard<std::mutex> lock(error_mutex);
              errors.emplace_back("param route returned unexpected body");
            }
          }
        } catch (const std::exception& ex) {
          std::lock_guard<std::mutex> lock(error_mutex);
          errors.emplace_back(std::string("request failed: ") + ex.what());
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

  EXPECT_TRUE(errors.empty()) << (errors.empty() ? "" : errors.front());
}

TEST(StressRoutingAcceptanceTest, DistinctParamTargetsRemainStableUnderLoad) {
  const auto cfg = LoadStressConfig(6, 60, 120000);

  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(cfg.threads);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/v/{x}/items/{y}",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        const auto* x = task->GetPathParameter("x");
        const auto* y = task->GetPathParameter("y");
        if (x == nullptr || y == nullptr) {
          task->GetResponse().result(http::status::bad_request);
          task->SetBody("missing-param");
          return;
        }
        task->SetBody(*x + ":" + *y);
      });

  bsrvcore::test::ServerGuard guard(std::move(server));
  const auto port = bsrvcore::test::FindFreePort();
  guard.server->AddListen({boost::asio::ip::make_address("127.0.0.1"), port});
  ASSERT_TRUE(guard.server->Start(cfg.threads));

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);
  std::vector<std::jthread> workers;
  std::mutex error_mutex;
  std::vector<std::string> errors;

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      sync.arrive_and_wait();
      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        try {
          const auto x = std::to_string(t);
          const auto y = std::to_string(i);
          const auto response = bsrvcore::test::DoRequestWithRetry(
              http::verb::get, port, "/v/" + x + "/items/" + y, "");
          if (response.result() != http::status::ok ||
              response.body() != x + ":" + y) {
            std::lock_guard<std::mutex> lock(error_mutex);
            errors.emplace_back("multi-param route returned unexpected body");
          }
        } catch (const std::exception& ex) {
          std::lock_guard<std::mutex> lock(error_mutex);
          errors.emplace_back(std::string("request failed: ") + ex.what());
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

  EXPECT_TRUE(errors.empty()) << (errors.empty() ? "" : errors.front());
}

}  // namespace
