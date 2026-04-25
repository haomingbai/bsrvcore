/**
 * @file websocket_client_task.cc
 * @brief Public factory and light forwarding layer for WebSocketClientTask.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-05
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/client/websocket_client_task.h"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "impl/default_client_ssl_context.h"

namespace bsrvcore {

namespace {

namespace http = boost::beast::http;

void ConfigureUpgradeHeaders(HttpClientRequest& request) {
  request.method(http::verb::get);
  request.set(http::field::connection, "Upgrade");
  request.set(http::field::upgrade, "websocket");
  request.set(http::field::sec_websocket_version, "13");
  // Placeholder key used for the first implementation stage.
  request.set(http::field::sec_websocket_key, "dGhlIHNhbXBsZSBub25jZQ==");
}

bool ParseWebSocketUrl(const std::string& url, bool* out_ssl, std::string* host,
                       std::string* port, std::string* target) {
  constexpr const char* kWsPrefix = "ws://";
  constexpr const char* kWssPrefix = "wss://";
  std::size_t offset = 0;

  if (url.rfind(kWsPrefix, 0) == 0) {
    *out_ssl = false;
    offset = 5;
  } else if (url.rfind(kWssPrefix, 0) == 0) {
    *out_ssl = true;
    offset = 6;
  } else {
    return false;
  }

  const auto slash = url.find('/', offset);
  const auto host_port = slash == std::string::npos
                             ? url.substr(offset)
                             : url.substr(offset, slash - offset);
  *target = slash == std::string::npos ? "/" : url.substr(slash);

  const auto colon = host_port.rfind(':');
  if (colon == std::string::npos) {
    *host = host_port;
    *port = *out_ssl ? "443" : "80";
  } else {
    *host = host_port.substr(0, colon);
    *port = host_port.substr(colon + 1);
  }

  return !host->empty() && !port->empty() && !target->empty();
}

}  // namespace

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::CreateHttp(
    Executor io_executor, std::string host, std::string port,
    std::string target, HandlerPtr handler, HttpClientOptions options) {
  auto ws_task = AllocateShared<WebSocketClientTask>(
      std::move(io_executor), std::move(host), std::move(port),
      std::move(target), std::move(handler), std::move(options), false,
      nullptr);
  ConfigureUpgradeHeaders(ws_task->Request());
  return ws_task;
}

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::CreateHttps(
    Executor io_executor, std::string host, std::string port,
    std::string target, HandlerPtr handler, HttpClientOptions options) {
  const auto& ssl_state =
      connection_internal::GetDefaultClientSslContextState();
  auto ws_task = AllocateShared<WebSocketClientTask>(
      std::move(io_executor), std::move(host), std::move(port),
      std::move(target), std::move(handler), std::move(options), true,
      ssl_state.ssl_ctx);
  if (ssl_state.ec) {
    ws_task->SetCreateError(ssl_state.ec,
                            ssl_state.error_message.empty()
                                ? "failed to initialize system SSL context"
                                : ssl_state.error_message);
  }
  ConfigureUpgradeHeaders(ws_task->Request());
  return ws_task;
}

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::CreateHttps(
    Executor io_executor, SslContextPtr ssl_ctx, std::string host,
    std::string port, std::string target, HandlerPtr handler,
    HttpClientOptions options) {
  auto ws_task = AllocateShared<WebSocketClientTask>(
      std::move(io_executor), std::move(host), std::move(port),
      std::move(target), std::move(handler), std::move(options), true,
      std::move(ssl_ctx));
  ConfigureUpgradeHeaders(ws_task->Request());
  return ws_task;
}

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::CreateFromUrl(
    Executor io_executor, std::string url, HandlerPtr handler,
    HttpClientOptions options) {
  bool use_ssl = false;
  std::string host;
  std::string port;
  std::string target;
  if (!ParseWebSocketUrl(url, &use_ssl, &host, &port, &target)) {
    return nullptr;
  }

  SslContextPtr ssl_ctx;
  if (use_ssl) {
    const auto& ssl_state =
        connection_internal::GetDefaultClientSslContextState();
    ssl_ctx = ssl_state.ssl_ctx;
    if (ssl_state.ec) {
      auto ws_task = AllocateShared<WebSocketClientTask>(
          std::move(io_executor), std::move(host), std::move(port),
          std::move(target), std::move(handler), std::move(options), true,
          nullptr);
      ws_task->SetCreateError(ssl_state.ec,
                              ssl_state.error_message.empty()
                                  ? "failed to initialize system SSL context"
                                  : ssl_state.error_message);
      ConfigureUpgradeHeaders(ws_task->Request());
      return ws_task;
    }
  }

  auto ws_task = AllocateShared<WebSocketClientTask>(
      std::move(io_executor), std::move(host), std::move(port),
      std::move(target), std::move(handler), std::move(options), use_ssl,
      std::move(ssl_ctx));
  ConfigureUpgradeHeaders(ws_task->Request());
  return ws_task;
}

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::CreateFromUrl(
    Executor io_executor, SslContextPtr ssl_ctx, std::string url,
    HandlerPtr handler, HttpClientOptions options) {
  bool use_ssl = false;
  std::string host;
  std::string port;
  std::string target;
  if (!ParseWebSocketUrl(url, &use_ssl, &host, &port, &target)) {
    return nullptr;
  }

  auto ws_task = AllocateShared<WebSocketClientTask>(
      std::move(io_executor), std::move(host), std::move(port),
      std::move(target), std::move(handler), std::move(options), use_ssl,
      use_ssl ? std::move(ssl_ctx) : SslContextPtr{});
  ConfigureUpgradeHeaders(ws_task->Request());
  return ws_task;
}

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::CreateHttpRaw(
    Executor io_executor, TcpStream stream, std::string host,
    std::string target, HandlerPtr handler, HttpClientOptions options) {
  auto ws_task = AllocateShared<WebSocketClientTask>(
      std::move(io_executor), std::move(host), "", std::move(target),
      std::move(handler), std::move(options), false, nullptr);
  ws_task->SetRawTcpStream(std::move(stream));
  ConfigureUpgradeHeaders(ws_task->Request());
  return ws_task;
}

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::CreateHttpsRaw(
    Executor io_executor, SslStream stream, std::string host,
    std::string target, HandlerPtr handler, HttpClientOptions options) {
  auto ws_task = AllocateShared<WebSocketClientTask>(
      std::move(io_executor), std::move(host), "", std::move(target),
      std::move(handler), std::move(options), true, nullptr);
  ws_task->SetRawSslStream(std::move(stream));
  ConfigureUpgradeHeaders(ws_task->Request());
  return ws_task;
}

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::OnHttpDone(
    HttpDoneCallback cb) {
  on_http_done_ = std::move(cb);
  return shared_from_this();
}

HttpClientRequest& WebSocketClientTask::Request() noexcept { return request_; }

void WebSocketClientTask::AttachSession(
    std::weak_ptr<HttpClientSession> session) {
  session_ = std::move(session);
}

WebSocketClientTask::~WebSocketClientTask() = default;

WebSocketClientTask::WebSocketClientTask(Executor io_executor, std::string host,
                                         std::string port, std::string target,
                                         HandlerPtr handler,
                                         HttpClientOptions options,
                                         bool use_ssl, SslContextPtr ssl_ctx)
    : WebSocketTaskBase(std::move(handler)),
      io_executor_(std::move(io_executor)),
      host_(std::move(host)),
      port_(std::move(port)),
      target_(std::move(target)),
      options_(std::move(options)),
      use_ssl_(use_ssl),
      ssl_ctx_(std::move(ssl_ctx)) {
  request_.method(http::verb::get);
  request_.target(target_);
  request_.version(11);
  request_.set(http::field::host, host_);
}

void WebSocketClientTask::SetCreateError(boost::system::error_code ec,
                                         std::string message) {
  create_error_ = ec;
  create_error_message_ = std::move(message);
}

void WebSocketClientTask::SetRawTcpStream(TcpStream stream) {
  startup_mode_ = StartupMode::kRawTcp;
  raw_ssl_stream_.reset();
  raw_tcp_stream_ = std::make_unique<TcpStream>(std::move(stream));
}

void WebSocketClientTask::SetRawSslStream(SslStream stream) {
  startup_mode_ = StartupMode::kRawTls;
  raw_tcp_stream_.reset();
  raw_ssl_stream_ = std::make_unique<SslStream>(std::move(stream));
}

bool WebSocketClientTask::IsOpen() const noexcept {
  return lifecycle_state_ == LifecycleState::kOpen;
}

bool WebSocketClientTask::IsClosingOrClosed() const noexcept {
  return lifecycle_state_ == LifecycleState::kClosing ||
         lifecycle_state_ == LifecycleState::kClosed;
}

void WebSocketClientTask::EmitHttpDone(boost::system::error_code ec,
                                       HttpResponseHeader header,
                                       HttpClientResponse response) {
  HttpClientResult result;
  result.ec = ec;
  result.stage = HttpClientStage::kDone;
  result.header = std::move(header);
  result.response = std::move(response);

  if (on_http_done_) {
    on_http_done_(result);
  }
}

}  // namespace bsrvcore
