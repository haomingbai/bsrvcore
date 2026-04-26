/**
 * @file http_proxy_request.cc
 * @brief HTTPS request through an HTTP proxy using ProxyConfig.
 *
 * Demonstrates:
 * - configuring an HTTP/HTTPS proxy via HttpClientOptions::proxy
 * - ProxyRequestAssembler rewriting request targets for HTTP proxy
 * - ProxyStreamBuilder establishing CONNECT tunnel + TLS for HTTPS proxy
 * - automatic proxy detection in HttpClientTask factory functions
 *
 * Usage:
 *   Requires a local HTTP proxy listening on 127.0.0.1:7890
 *   (e.g. clash, sing-box, v2ray, or any HTTP CONNECT-capable proxy).
 *
 *   ./example_client_http_proxy_request
 */

#include <bsrvcore/connection/client/http_client_task.h>

#include <boost/asio/io_context.hpp>
#include <boost/beast/http/verb.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <string>

int main() {
  namespace http = boost::beast::http;

  bsrvcore::IoContext ioc;

  bsrvcore::HttpClientOptions options;
  options.resolve_timeout = std::chrono::seconds(5);
  options.connect_timeout = std::chrono::seconds(5);
  options.tls_handshake_timeout = std::chrono::seconds(5);
  options.write_timeout = std::chrono::seconds(5);
  options.read_header_timeout = std::chrono::seconds(5);
  options.read_body_timeout = std::chrono::seconds(5);
  options.user_agent = "bsrvcore-example-proxy";

  // Configure proxy: all requests go through 127.0.0.1:7890.
  // For HTTPS targets, ProxyStreamBuilder establishes a CONNECT tunnel
  // and performs TLS handshake through the proxy.
  options.proxy.host = "127.0.0.1";
  options.proxy.port = "7890";
  // options.proxy.auth = "Basic dXNlcjpwYXNz";  // Uncomment if proxy
  // requires auth.

  // Create an HTTPS task. The factory automatically detects proxy
  // configuration and wraps the assembler/builder with proxy support.
  auto task = bsrvcore::HttpClientTask::CreateHttps(
      ioc.get_executor(), "www.google.com", "443", "/", http::verb::get,
      options);

  std::promise<bsrvcore::HttpClientResult> promise;
  auto future = promise.get_future();

  task->OnDone([&promise](const bsrvcore::HttpClientResult& result) {
    promise.set_value(result);
  });

  task->Start();
  ioc.run();

  auto result = future.get();
  if (result.ec) {
    std::cerr << "Proxy request failed: " << result.ec.message() << '\n';
    return 1;
  }

  std::cout << "Status: " << result.response.result_int() << '\n';
  std::cout << "Body size: " << result.response.body().size() << '\n';
  return 0;
}
