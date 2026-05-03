#include <gtest/gtest.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/session/attribute.h"
#include "bsrvcore/session/context.h"
#include "test_http_client_task.h"

namespace {
using bsrvcore::test::DoRequestWithRetry;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;
namespace http = boost::beast::http;

class StoredValueAttribute
    : public bsrvcore::CloneableAttribute<StoredValueAttribute> {
 public:
  explicit StoredValueAttribute(std::string value) : value(std::move(value)) {}

  std::string value;
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

}  // namespace

TEST(SessionAcceptanceTest, ExistingSessionCookieIsReusedWithoutReset) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/session",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody(task->GetSessionId());
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto res = DoRequestWithRetry(
      http::verb::get, port, "/session", "",
      [](http::request<http::string_body>& request) {
        request.set(http::field::cookie, "a=1; sessionId=abc; b=2");
      });

  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "abc");
  EXPECT_EQ(res.base().count(http::field::set_cookie), 0u);
}

TEST(SessionAcceptanceTest, GeneratedSessionCookiePreservesSessionState) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/remember/{value}",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        const auto* value = task->GetPathParameter("value");
        ASSERT_NE(value, nullptr);

        auto session = task->GetSession();
        ASSERT_NE(session, nullptr);

        auto stored = std::dynamic_pointer_cast<StoredValueAttribute>(
            session->GetAttribute("stored"));
        if (!stored) {
          session->SetAttribute("stored",
                                std::make_shared<StoredValueAttribute>(*value));
          stored = std::dynamic_pointer_cast<StoredValueAttribute>(
              session->GetAttribute("stored"));
        }

        ASSERT_NE(stored, nullptr);
        EXPECT_TRUE(task->SetSessionTimeout(60 * 1000));
        task->SetBody(stored->value);
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto first =
      DoRequestWithRetry(http::verb::get, port, "/remember/first", "");
  EXPECT_EQ(first.result(), http::status::ok);
  EXPECT_EQ(first.body(), "first");
  ASSERT_GT(first.base().count(http::field::set_cookie), 0u);

  const auto session_cookie = ExtractSessionCookie(first);
  ASSERT_FALSE(session_cookie.empty());
  EXPECT_NE(session_cookie.find("sessionId="), std::string::npos);

  const auto second = DoRequestWithRetry(
      http::verb::get, port, "/remember/second", "",
      [&session_cookie](http::request<http::string_body>& request) {
        request.set(http::field::cookie, session_cookie);
      });

  EXPECT_EQ(second.result(), http::status::ok);
  EXPECT_EQ(second.body(), "first");
  EXPECT_EQ(second.base().count(http::field::set_cookie), 0u);
}
