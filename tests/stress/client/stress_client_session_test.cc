#include <gtest/gtest.h>

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "bsrvcore/connection/client/http_client_session.h"
#include "bsrvcore/connection/client/http_sse_client_task.h"
#include "bsrvcore/connection/client/sse_event_parser.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/connection/server/server_set_cookie.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"
#include "stress_test_common.h"
#include "test_http_client_task.h"

namespace {

namespace http = boost::beast::http;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;
using bsrvcore::test::stress::LoadStressConfig;

bsrvcore::HttpClientResponse DoSessionRequest(
    const std::shared_ptr<bsrvcore::HttpClientSession>& session,
    http::verb method, unsigned short port, const std::string& target) {
  bsrvcore::IoContext ioc;
  auto task = session->CreateHttp(ioc.get_executor(), "127.0.0.1",
                                  std::to_string(port), target, method);

  std::promise<bsrvcore::HttpClientResult> promise;
  auto future = promise.get_future();
  task->OnDone(
      [&](const bsrvcore::HttpClientResult& r) { promise.set_value(r); });
  task->Start();
  ioc.run();

  auto result = future.get();
  if (result.ec) {
    throw boost::system::system_error(result.ec);
  }
  return result.response;
}

TEST(StressClientSessionTest, ConcurrentClientTasksWithSharedCookieJar) {
  const auto cfg = LoadStressConfig(8, 120, 120000);

  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(cfg.threads);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/set",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        bsrvcore::ServerSetCookie cookie;
        cookie.SetName("cid").SetValue("client").SetPath("/").SetMaxAge(3600);
        task->AddCookie(std::move(cookie));
        task->SetBody("ok");
      });

  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/echo-cookie",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody(task->GetCookie("cid"));
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  auto session = bsrvcore::HttpClientSession::Create();
  ASSERT_NO_THROW(
      (void)DoSessionRequest(session, http::verb::get, port, "/set"));

  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        auto res =
            DoSessionRequest(session, http::verb::get, port, "/echo-cookie");
        EXPECT_EQ(res.result(), http::status::ok);
        EXPECT_EQ(res.body(), "client");
      }
    });
  }

  for (auto& th : workers) {
    th.join();
  }
}

TEST(StressClientSessionTest, SseClientPullsBurstEvents) {
  const auto cfg = LoadStressConfig(6, 100, 120000);

  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(cfg.threads);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/events",
                        [cfg](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetField(http::field::content_type,
                                         "text/event-stream; charset=utf-8");

                          std::string payload;
                          payload.reserve(cfg.iterations * 16);
                          for (std::size_t i = 0; i < cfg.iterations; ++i) {
                            payload += "data: ev-" + std::to_string(i) + "\n\n";
                          }
                          task->SetBody(std::move(payload));
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  bsrvcore::IoContext ioc;
  auto client = bsrvcore::HttpSseClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), "/events");

  auto parser = bsrvcore::AllocateShared<bsrvcore::SseEventParser>();
  auto events = bsrvcore::AllocateShared<std::vector<bsrvcore::SseEvent>>();
  auto completion = bsrvcore::AllocateShared<std::promise<void>>();
  auto future = completion->get_future();
  auto done = bsrvcore::AllocateShared<bool>(false);

  std::function<void()> pull_next;
  pull_next = [client, parser, events, completion, done, &pull_next]() {
    client->Next([parser, events, completion, done,
                  &pull_next](const bsrvcore::HttpSseClientResult& result) {
      if (*done) {
        return;
      }

      if (result.ec && !result.cancelled) {
        *done = true;
        completion->set_exception(
            std::make_exception_ptr(boost::system::system_error(result.ec)));
        return;
      }

      if (!result.chunk.empty()) {
        auto parsed = parser->Feed(result.chunk);
        events->insert(events->end(), parsed.begin(), parsed.end());
      }

      if (result.eof || result.cancelled) {
        *done = true;
        completion->set_value();
        return;
      }

      pull_next();
    });
  };

  client->Start([completion, done,
                 &pull_next](const bsrvcore::HttpSseClientResult& result) {
    if (result.ec || result.cancelled) {
      if (!*done) {
        *done = true;
        completion->set_exception(
            std::make_exception_ptr(boost::system::system_error(result.ec)));
      }
      return;
    }
    pull_next();
  });

  ioc.run();
  EXPECT_NO_THROW(future.get());
  EXPECT_GE(events->size(), cfg.iterations);
}

}  // namespace
