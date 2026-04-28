/**
 * @file http_client_session.cc
 * @brief HttpClientSession task factory implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/client/http_client_session.h"

#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/errc.hpp>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/stream_builder.h"
#include "bsrvcore/connection/client/websocket_client_task.h"
#include "impl/default_client_assembler_builder.h"
#include "impl/default_client_ssl_context.h"
#include "impl/http_url_parser.h"

namespace bsrvcore {

namespace {

namespace http = boost::beast::http;

using connection_internal::ParseHttpUrl;

}  // namespace

std::shared_ptr<HttpClientSession> HttpClientSession::Create() {
  void* raw = Allocate(sizeof(HttpClientSession), alignof(HttpClientSession));
  try {
    auto* session = new (raw) HttpClientSession();
    return {session, [](HttpClientSession* ptr) { DestroyDeallocate(ptr); }};
  } catch (...) {
    Deallocate(raw, sizeof(HttpClientSession), alignof(HttpClientSession));
    throw;
  }
}

// ---- HTTP factories ----

std::shared_ptr<HttpClientTask> HttpClientSession::CreateHttp(
    HttpClientTask::Executor io_executor, std::string host, std::string port,
    std::string target, HttpVerb method, HttpClientOptions options) {
  return CreateHttp(io_executor, io_executor, std::move(host), std::move(port),
                    std::move(target), method, std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateHttp(
    HttpClientTask::Executor io_executor,
    HttpClientTask::Executor callback_executor, std::string host,
    std::string port, std::string target, HttpVerb method,
    HttpClientOptions options) {
  return HttpClientTask::CreateSessionTask(
      shared_from_this(), std::move(io_executor), std::move(callback_executor),
      "http", std::move(host), std::move(port), std::move(target), method,
      std::move(options), nullptr);
}

// ---- HTTPS factories ----

std::shared_ptr<HttpClientTask> HttpClientSession::CreateHttps(
    HttpClientTask::Executor io_executor, std::string host, std::string port,
    std::string target, HttpVerb method, HttpClientOptions options) {
  return CreateHttps(io_executor, io_executor, std::move(host), std::move(port),
                     std::move(target), method, std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateHttps(
    HttpClientTask::Executor io_executor,
    HttpClientTask::Executor callback_executor, std::string host,
    std::string port, std::string target, HttpVerb method,
    HttpClientOptions options) {
  const auto& ssl_state =
      connection_internal::GetDefaultClientSslContextState();
  return HttpClientTask::CreateSessionTask(
      shared_from_this(), std::move(io_executor), std::move(callback_executor),
      "https", std::move(host), std::move(port), std::move(target), method,
      std::move(options), ssl_state.ssl_ctx, ssl_state.ec);
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateHttps(
    HttpClientTask::Executor io_executor, SslContextPtr ssl_ctx,
    std::string host, std::string port, std::string target, HttpVerb method,
    HttpClientOptions options) {
  return CreateHttps(io_executor, io_executor, std::move(ssl_ctx),
                     std::move(host), std::move(port), std::move(target),
                     method, std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateHttps(
    HttpClientTask::Executor io_executor,
    HttpClientTask::Executor callback_executor, SslContextPtr ssl_ctx,
    std::string host, std::string port, std::string target, HttpVerb method,
    HttpClientOptions options) {
  return HttpClientTask::CreateSessionTask(
      shared_from_this(), std::move(io_executor), std::move(callback_executor),
      "https", std::move(host), std::move(port), std::move(target), method,
      std::move(options), std::move(ssl_ctx));
}

// ---- URL factories ----

std::shared_ptr<HttpClientTask> HttpClientSession::CreateFromUrl(
    HttpClientTask::Executor io_executor, std::string url, HttpVerb method,
    HttpClientOptions options) {
  return CreateFromUrl(io_executor, io_executor, std::move(url), method,
                       std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateFromUrl(
    HttpClientTask::Executor io_executor,
    HttpClientTask::Executor callback_executor, std::string url,
    HttpVerb method, HttpClientOptions options) {
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    return HttpClientTask::CreateSessionTask(
        shared_from_this(), std::move(io_executor),
        std::move(callback_executor), "http", "", "", "/", method,
        std::move(options), nullptr,
        make_error_code(boost::system::errc::invalid_argument));
  }

  if (parsed->https) {
    const auto& ssl_state =
        connection_internal::GetDefaultClientSslContextState();
    return HttpClientTask::CreateSessionTask(
        shared_from_this(), std::move(io_executor),
        std::move(callback_executor), "https", std::move(parsed->host),
        std::move(parsed->port), std::move(parsed->target), method,
        std::move(options), ssl_state.ssl_ctx, ssl_state.ec);
  }

  return HttpClientTask::CreateSessionTask(
      shared_from_this(), std::move(io_executor), std::move(callback_executor),
      "http", std::move(parsed->host), std::move(parsed->port),
      std::move(parsed->target), method, std::move(options), nullptr);
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateFromUrl(
    HttpClientTask::Executor io_executor, SslContextPtr ssl_ctx,
    std::string url, HttpVerb method, HttpClientOptions options) {
  return CreateFromUrl(io_executor, io_executor, std::move(ssl_ctx),
                       std::move(url), method, std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateFromUrl(
    HttpClientTask::Executor io_executor,
    HttpClientTask::Executor callback_executor, SslContextPtr ssl_ctx,
    std::string url, HttpVerb method, HttpClientOptions options) {
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    return HttpClientTask::CreateSessionTask(
        shared_from_this(), std::move(io_executor),
        std::move(callback_executor), "http", "", "", "/", method,
        std::move(options), nullptr,
        make_error_code(boost::system::errc::invalid_argument));
  }

  auto effective_ssl_ctx = parsed->https ? std::move(ssl_ctx) : SslContextPtr{};
  return HttpClientTask::CreateSessionTask(
      shared_from_this(), std::move(io_executor), std::move(callback_executor),
      parsed->https ? "https" : "http", std::move(parsed->host),
      std::move(parsed->port), std::move(parsed->target), method,
      std::move(options), std::move(effective_ssl_ctx));
}

// ---- WebSocket factories (use session as assembler for cookie management)
// ----

std::shared_ptr<WebSocketClientTask> HttpClientSession::CreateWebSocketHttp(
    HttpClientTask::Executor io_executor, std::string host, std::string port,
    std::string target, WebSocketClientTask::HandlerPtr handler,
    HttpClientOptions options) {
  auto task = WebSocketClientTask::CreateHttp(
      std::move(io_executor), std::move(host), std::move(port),
      std::move(target), std::move(handler), std::move(options));
  if (task) {
    // Replace default assembler with session's SessionRequestAssembler.
    auto builder = connection_internal::GetDefaultDirectStreamBuilder();
    task->SetAssembler(shared_from_this(), builder);
  }
  return task;
}

std::shared_ptr<WebSocketClientTask> HttpClientSession::CreateWebSocketHttps(
    HttpClientTask::Executor io_executor, std::string host, std::string port,
    std::string target, WebSocketClientTask::HandlerPtr handler,
    HttpClientOptions options) {
  auto task = WebSocketClientTask::CreateHttps(
      std::move(io_executor), std::move(host), std::move(port),
      std::move(target), std::move(handler), std::move(options));
  if (task) {
    // Replace with session's assembler and a WebSocketStreamBuilder for WSS.
    const auto& ssl_state =
        connection_internal::GetDefaultClientSslContextState();
    auto builder = WebSocketStreamBuilder::Create(connection_internal::GetDefaultDirectStreamBuilder(),
                                                  ssl_state.ssl_ctx);
    task->SetAssembler(shared_from_this(), builder);
  }
  return task;
}

std::shared_ptr<WebSocketClientTask> HttpClientSession::CreateWebSocketHttps(
    HttpClientTask::Executor io_executor, SslContextPtr ssl_ctx,
    std::string host, std::string port, std::string target,
    WebSocketClientTask::HandlerPtr handler, HttpClientOptions options) {
  auto task = WebSocketClientTask::CreateHttps(
      std::move(io_executor), ssl_ctx, std::move(host), std::move(port),
      std::move(target), std::move(handler), std::move(options));
  if (task) {
    // Replace with session's assembler and a WebSocketStreamBuilder for WSS.
    auto builder =
        WebSocketStreamBuilder::Create(connection_internal::GetDefaultDirectStreamBuilder(), ssl_ctx);
    task->SetAssembler(shared_from_this(), builder);
  }
  return task;
}

std::shared_ptr<WebSocketClientTask> HttpClientSession::CreateWebSocketFromUrl(
    HttpClientTask::Executor io_executor, std::string url,
    WebSocketClientTask::HandlerPtr handler, HttpClientOptions options) {
  auto task = WebSocketClientTask::CreateFromUrl(
      std::move(io_executor), std::move(url), std::move(handler),
      std::move(options));
  if (task) {
    // Replace only the assembler (session for cookie management).
    // The builder (WebSocketStreamBuilder for WSS) is already set correctly.
    task->SetAssemblerOnly(shared_from_this());
  }
  return task;
}

std::shared_ptr<WebSocketClientTask> HttpClientSession::CreateWebSocketFromUrl(
    HttpClientTask::Executor io_executor, SslContextPtr ssl_ctx,
    std::string url, WebSocketClientTask::HandlerPtr handler,
    HttpClientOptions options) {
  auto task = WebSocketClientTask::CreateFromUrl(
      std::move(io_executor), std::move(ssl_ctx), std::move(url),
      std::move(handler), std::move(options));
  if (task) {
    // Replace only the assembler (session for cookie management).
    // The builder (WebSocketStreamBuilder for WSS) is already set correctly.
    task->SetAssemblerOnly(shared_from_this());
  }
  return task;
}

}  // namespace bsrvcore
