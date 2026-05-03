#include <gtest/gtest.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/stream_slot.h"
#include "bsrvcore/core/types.h"

namespace {
namespace http = boost::beast::http;

bsrvcore::HttpClientRequest MakeRequest(http::verb method, std::string target) {
  bsrvcore::HttpClientRequest request;
  request.method(method);
  request.target(std::move(target));
  request.version(11);
  return request;
}

}  // namespace

TEST(ProxyRequestAssemblerTest, HttpTargetBecomesAbsoluteFormAndRecordsProxy) {
  auto inner = std::make_shared<bsrvcore::DefaultRequestAssembler>();

  bsrvcore::ProxyConfig proxy;
  proxy.host = "proxy.local";
  proxy.port = "3128";
  proxy.auth = "Basic dXNlcjpwYXNz";

  bsrvcore::ProxyRequestAssembler assembler(inner, proxy);
  bsrvcore::HttpClientOptions options;

  auto result =
      assembler.Assemble(MakeRequest(http::verb::get, "/v1/data?x=1"), options,
                         "http", "api.example.com", "8080", nullptr);

  EXPECT_EQ(result.request.target(), "http://api.example.com:8080/v1/data?x=1");
  EXPECT_EQ(result.connection_key.scheme, "http");
  EXPECT_EQ(result.connection_key.host, "api.example.com");
  EXPECT_EQ(result.connection_key.port, "8080");
  EXPECT_EQ(result.connection_key.proxy_host, "proxy.local");
  EXPECT_EQ(result.connection_key.proxy_port, "3128");
  EXPECT_EQ(result.request[http::field::proxy_authorization],
            "Basic dXNlcjpwYXNz");
}

TEST(ProxyRequestAssemblerTest, HttpsTargetPreservesOriginFormAndTunnelSslCtx) {
  auto inner = std::make_shared<bsrvcore::DefaultRequestAssembler>();

  bsrvcore::ProxyConfig proxy;
  proxy.host = "proxy.local";
  proxy.port = "3128";

  bsrvcore::ProxyRequestAssembler assembler(inner, proxy);
  bsrvcore::HttpClientOptions options;
  auto ssl_ctx =
      std::make_shared<bsrvcore::SslContext>(bsrvcore::SslContext::tls_client);

  auto result =
      assembler.Assemble(MakeRequest(http::verb::get, "/secure"), options,
                         "https", "secure.example.com", "443", ssl_ctx);

  EXPECT_EQ(result.request.target(), "/secure");
  EXPECT_EQ(result.connection_key.scheme, "https");
  EXPECT_EQ(result.connection_key.host, "secure.example.com");
  EXPECT_EQ(result.connection_key.port, "443");
  EXPECT_EQ(result.connection_key.proxy_host, "proxy.local");
  EXPECT_EQ(result.connection_key.proxy_port, "3128");
  EXPECT_EQ(result.connection_key.ssl_ctx, nullptr);
  EXPECT_EQ(result.connection_key.proxy_ssl_ctx, ssl_ctx);
}

TEST(SessionRequestAssemblerTest, FutureExpiresCookieIsStoredAndInjected) {
  bsrvcore::SessionRequestAssembler assembler;

  assembler.SyncSetCookie("example.com", "/prefs/theme",
                          "sid=alive; Path=/prefs; "
                          "Expires=Wed, 31 Dec 2098 23:59:59 GMT");

  EXPECT_EQ(assembler.CookieCount(), 1U);

  auto request = MakeRequest(http::verb::get, "/prefs/theme");
  assembler.MaybeInjectCookies(request, "example.com", request.target(), false);

  EXPECT_EQ(request[http::field::cookie], "sid=alive");
}

TEST(SessionRequestAssemblerTest, PastExpiresCookieDeletesMatchingCookie) {
  bsrvcore::SessionRequestAssembler assembler;

  assembler.SyncSetCookie("example.com", "/prefs/theme",
                          "sid=alive; Path=/prefs; "
                          "Expires=Wed, 31 Dec 2098 23:59:59 GMT");
  ASSERT_EQ(assembler.CookieCount(), 1U);

  assembler.SyncSetCookie("example.com", "/prefs/theme",
                          "sid=expired; Path=/prefs; "
                          "Expires=Thu, 01 Jan 1970 00:00:00 GMT");

  EXPECT_EQ(assembler.CookieCount(), 0U);
}
