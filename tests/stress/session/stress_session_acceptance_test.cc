#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/session/attribute.h"
#include "bsrvcore/session/context.h"
#include "stress_test_common.h"
#include "test_http_client_task.h"

namespace {

using bsrvcore::test::stress::LoadStressConfig;
using bsrvcore::test::stress::WaitCounter;
namespace http = boost::beast::http;

class CounterAttribute : public bsrvcore::CloneableAttribute<CounterAttribute> {
 public:
  CounterAttribute() : counter(std::make_shared<std::atomic<std::size_t>>(0)) {}

  std::shared_ptr<std::atomic<std::size_t>> counter;
};

std::string ExtractSessionCookie(
    const http::response<http::string_body>& response) {
  const auto count = response.base().count(http::field::set_cookie);
  if (count == 0) {
    return "";
  }

  const std::string header(response.base().at(http::field::set_cookie));
  const auto end = header.find(';');
  return header.substr(0, end);
}

TEST(StressSessionAcceptanceTest,
     ConcurrentRequestsWithSharedSessionCookieReuseState) {
  const auto cfg = LoadStressConfig(6, 40, 120000);
  SCOPED_TRACE(::testing::Message()
               << "threads=" << cfg.threads << " iterations=" << cfg.iterations
               << " seed=" << cfg.seed
               << " timeout_ms=" << cfg.timeout.count());

  auto server = std::make_unique<bsrvcore::HttpServer>(cfg.threads);
  server->SetDefaultSessionTimeout(60 * 1000)->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/session/counter",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        auto session = task->GetSession();
        ASSERT_NE(session, nullptr);

        auto counter = std::dynamic_pointer_cast<CounterAttribute>(
            session->GetAttribute("counter"));
        if (!counter) {
          session->SetAttribute("counter",
                                std::make_shared<CounterAttribute>());
          counter = std::dynamic_pointer_cast<CounterAttribute>(
              session->GetAttribute("counter"));
        }

        ASSERT_NE(counter, nullptr);
        EXPECT_TRUE(task->SetSessionTimeout(120 * 1000));
        const auto current =
            counter->counter->fetch_add(1, std::memory_order_relaxed) + 1;
        task->SetBody(std::to_string(current));
      });

  bsrvcore::test::ServerGuard guard(std::move(server));
  const auto port = bsrvcore::test::FindFreePort();
  guard.server->AddListen({boost::asio::ip::make_address("127.0.0.1"), port},
                          cfg.threads);
  ASSERT_TRUE(guard.server->Start());

  const std::string session_cookie = "sessionId=stable-session";
  const auto prewarm = bsrvcore::test::DoRequestWithRetry(
      http::verb::get, port, "/session/counter", "",
      [&session_cookie](http::request<http::string_body>& request) {
        request.set(http::field::cookie, session_cookie);
      });
  ASSERT_EQ(prewarm.result(), http::status::ok);
  ASSERT_EQ(prewarm.body(), "1");

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);
  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);
  std::mutex error_mutex;
  std::vector<std::string> errors;

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&](std::stop_token st) {
      sync.arrive_and_wait();

      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        try {
          const auto response = bsrvcore::test::DoRequestWithRetry(
              http::verb::get, port, "/session/counter", "",
              [&session_cookie](http::request<http::string_body>& request) {
                request.set(http::field::cookie, session_cookie);
              });
          if (response.result() != http::status::ok ||
              response.body().empty()) {
            std::lock_guard<std::mutex> lock(error_mutex);
            errors.emplace_back(
                "shared-session request returned unexpected response");
          }
          if (response.base().count(http::field::set_cookie) != 0u) {
            std::lock_guard<std::mutex> lock(error_mutex);
            errors.emplace_back(
                "shared-session request unexpectedly reset cookie");
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

  if (completed) {
    const auto final_response = bsrvcore::test::DoRequestWithRetry(
        http::verb::get, port, "/session/counter", "",
        [&session_cookie](http::request<http::string_body>& request) {
          request.set(http::field::cookie, session_cookie);
        });
    ASSERT_EQ(final_response.result(), http::status::ok);
    EXPECT_EQ(final_response.body(),
              std::to_string(cfg.threads * cfg.iterations + 2));
  }

  EXPECT_TRUE(errors.empty()) << (errors.empty() ? "" : errors.front());
}

TEST(StressSessionAcceptanceTest,
     ConcurrentAnonymousRequestsReceiveFreshSessionCookies) {
  const auto cfg = LoadStressConfig(6, 30, 120000);
  SCOPED_TRACE(::testing::Message()
               << "threads=" << cfg.threads << " iterations=" << cfg.iterations
               << " seed=" << cfg.seed
               << " timeout_ms=" << cfg.timeout.count());

  auto server = std::make_unique<bsrvcore::HttpServer>(cfg.threads);
  server->SetDefaultSessionTimeout(30 * 1000)->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/session/init",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetBody(task->GetSessionId());
      });

  bsrvcore::test::ServerGuard guard(std::move(server));
  const auto port = bsrvcore::test::FindFreePort();
  guard.server->AddListen({boost::asio::ip::make_address("127.0.0.1"), port},
                          cfg.threads);
  ASSERT_TRUE(guard.server->Start());

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);
  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);
  std::mutex error_mutex;
  std::vector<std::string> errors;

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&](std::stop_token st) {
      sync.arrive_and_wait();
      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        try {
          const auto response = bsrvcore::test::DoRequestWithRetry(
              http::verb::get, port, "/session/init", "");
          const auto session_cookie = ExtractSessionCookie(response);
          if (response.result() != http::status::ok ||
              response.body().empty() || session_cookie.empty() ||
              session_cookie.find("sessionId=") == std::string::npos) {
            std::lock_guard<std::mutex> lock(error_mutex);
            errors.emplace_back(
                "anonymous request failed to receive a session cookie");
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
