/**
 * @file http_proxy_request.cc
 * @brief HTTPS request through an HTTP proxy using the explicit 3-phase
 * client pipeline.
 *
 * Demonstrates:
 * - Phase 1: `ProxyRequestAssembler` assembling request + `ConnectionKey`
 * - Phase 2: `ProxyStreamBuilder` establishing CONNECT tunnel + TLS
 * - Phase 3: `HttpClientTask::CreateHttpsRaw(...)` consuming the ready stream
 *
 * Usage:
 *   Requires a local HTTP proxy listening on 127.0.0.1:7890
 *   (e.g. clash, sing-box, v2ray, or any HTTP CONNECT-capable proxy).
 *
 *   ./example_client_http_proxy_request
 */

#include <bsrvcore/connection/client/http_client_task.h>
#include <bsrvcore/connection/client/request_assembler.h>
#include <bsrvcore/connection/client/stream_builder.h>

#include <boost/asio/io_context.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <chrono>
#include <iostream>
#include <memory>
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

  bsrvcore::ProxyConfig proxy;
  proxy.host = "127.0.0.1";
  proxy.port = "7890";

  auto ssl_ctx =
      std::make_shared<bsrvcore::SslContext>(bsrvcore::SslContext::tls_client);
  boost::system::error_code verify_paths_ec;
  ssl_ctx->set_default_verify_paths(verify_paths_ec);
  if (verify_paths_ec) {
    std::cerr << "Failed to load system trust roots: "
              << verify_paths_ec.message() << '\n';
    return 1;
  }

  auto base_assembler = std::make_shared<bsrvcore::DefaultRequestAssembler>();
  auto assembler = std::make_shared<bsrvcore::ProxyRequestAssembler>(
      base_assembler, std::move(proxy));
  auto direct_builder = bsrvcore::DirectStreamBuilder::Create();
  auto builder = bsrvcore::ProxyStreamBuilder::Create(direct_builder);

  bsrvcore::HttpClientRequest request;
  request.method(http::verb::get);
  request.target("/");
  request.version(11);

  auto assembled = assembler->Assemble(std::move(request), options, "https",
                                       "www.google.com", "443", ssl_ctx);

  std::shared_ptr<bsrvcore::HttpClientTask> task;
  int exit_code = 1;
  bool finished = false;

  builder->Acquire(
      assembled.connection_key, ioc.get_executor(),
      [&](boost::system::error_code ec, bsrvcore::StreamSlot slot) mutable {
        if (ec) {
          std::cerr << "Stream acquisition failed: " << ec.message() << '\n';
          finished = true;
          return;
        }

        if (!slot.ssl_stream) {
          std::cerr << "Expected a TLS stream from proxy builder\n";
          finished = true;
          return;
        }

        task = bsrvcore::HttpClientTask::CreateHttpsRaw(
            ioc.get_executor(), std::move(*slot.ssl_stream),
            assembled.connection_key.host,
            std::string(assembled.request.target()), assembled.request.method(),
            options);
        task->Request() = std::move(assembled.request);
        task->OnDone([&](const bsrvcore::HttpClientResult& result) {
          finished = true;
          if (result.ec) {
            std::cerr << "Proxy request failed: " << result.ec.message()
                      << '\n';
            exit_code = 1;
            return;
          }

          std::cout << "Status: " << result.response.result_int() << '\n';
          std::cout << "Body size: " << result.response.body().size() << '\n';
          exit_code = 0;
        });
        task->Start();
      });

  ioc.run();

  if (!finished) {
    std::cerr << "Proxy example finished without producing a terminal result\n";
    return 1;
  }
  return exit_code;
}
