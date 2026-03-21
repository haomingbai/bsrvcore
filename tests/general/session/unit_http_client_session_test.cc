#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <string>

#include "bsrvcore/http_client_session.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "bsrvcore/server_set_cookie.h"
#include "test_http_client_task.h"

namespace {
namespace http = boost::beast::http;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;

inline bsrvcore::HttpClientResponse DoSessionRequest(
    const std::shared_ptr<bsrvcore::HttpClientSession>& session,
    http::verb method, unsigned short port, const std::string& target) {
  boost::asio::io_context ioc;
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

}  // namespace

TEST(HttpClientSessionTest, CookieRoundTripIsInjectedOnNextRequest) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/set",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        bsrvcore::ServerSetCookie c;
        c.SetName("foo").SetValue("bar").SetPath("/").SetMaxAge(3600);
        task->AddCookie(std::move(c));
        task->SetBody("ok");
      });
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/check",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody(task->GetCookie("foo"));
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  auto session = bsrvcore::HttpClientSession::Create();

  auto r1 = DoSessionRequest(session, http::verb::get, port, "/set");
  EXPECT_EQ(r1.result(), http::status::ok);
  EXPECT_GE(session->CookieCount(), 1U);

  auto r2 = DoSessionRequest(session, http::verb::get, port, "/check");
  EXPECT_EQ(r2.result(), http::status::ok);
  EXPECT_EQ(r2.body(), "bar");
}

TEST(HttpClientSessionTest, SecureCookieNotSentOverHttp) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/set_secure",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        bsrvcore::ServerSetCookie c;
        c.SetName("s").SetValue("1").SetPath("/").SetSecure(true).SetMaxAge(
            3600);
        task->AddCookie(std::move(c));
        task->SetBody("ok");
      });
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/check_secure",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody(task->GetCookie("s"));
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  auto session = bsrvcore::HttpClientSession::Create();
  DoSessionRequest(session, http::verb::get, port, "/set_secure");
  EXPECT_GE(session->CookieCount(), 1U);

  auto r = DoSessionRequest(session, http::verb::get, port, "/check_secure");
  EXPECT_EQ(r.result(), http::status::ok);
  EXPECT_EQ(r.body(), "");
}

TEST(HttpClientSessionTest, PathPrefixMatchingWorks) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/set_path",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        bsrvcore::ServerSetCookie c;
        c.SetName("p").SetValue("1").SetPath("/a").SetMaxAge(3600);
        task->AddCookie(std::move(c));
        task->SetBody("ok");
      });
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/a/x",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody(task->GetCookie("p"));
                        });
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/b",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody(task->GetCookie("p"));
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  auto session = bsrvcore::HttpClientSession::Create();
  DoSessionRequest(session, http::verb::get, port, "/set_path");

  auto ok = DoSessionRequest(session, http::verb::get, port, "/a/x");
  EXPECT_EQ(ok.body(), "1");

  auto miss = DoSessionRequest(session, http::verb::get, port, "/b");
  EXPECT_EQ(miss.body(), "");
}
