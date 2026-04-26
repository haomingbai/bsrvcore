/**
 * @file http_client_task.cc
 * @brief Public factory and forwarding layer for HttpClientTask.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/client/http_client_task.h"

#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/stream_builder.h"
#include "impl/default_client_ssl_context.h"
#include "impl/http_client_task_impl.h"
#include "impl/http_url_parser.h"

namespace bsrvcore {

namespace {

namespace json = bsrvcore::json;
namespace http = boost::beast::http;

using connection_internal::ParseHttpUrl;

JsonErrorCode ParseJsonText(std::string_view text, JsonValue& out) {
  JsonErrorCode ec;
  JsonValue parsed = json::parse(text, ec);
  if (ec) {
    return ec;
  }

  out = std::move(parsed);
  return {};
}

JsonErrorCode ParseJsonText(std::string_view text, JsonObject& out) {
  JsonValue parsed;
  JsonErrorCode ec = ParseJsonText(text, parsed);
  if (ec) {
    return ec;
  }

  if (!parsed.is_object()) {
    return make_error_code(json::error::not_object);
  }

  out = parsed.as_object();
  return {};
}

void SetJsonBody(HttpClientRequest& request, const JsonValue& value) {
  request.body() = json::serialize(value);
  request.set(http::field::content_type, "application/json");
}

}  // namespace

HttpClientTask::HttpClientTask(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

HttpClientTask::~HttpClientTask() = default;

std::shared_ptr<HttpClientTask> HttpClientTask::CreateAssembledTask(
    Executor io_executor, Executor callback_executor, std::string scheme,
    std::string host, std::string port, std::string target, http::verb method,
    HttpClientOptions options, SslContextPtr ssl_ctx,
    boost::system::error_code create_ec) {
  auto assembler =
      std::make_shared<DefaultRequestAssembler>(scheme, host, port, ssl_ctx);
  auto builder = DirectStreamBuilder::Create();
  assembler->SetStreamBuilder(builder);

  const bool use_ssl = (scheme == "https");
  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), host, port,
      std::move(target), method, std::move(options), use_ssl, ssl_ctx);
  impl->SetAssembler(assembler, builder);
  if (create_ec) {
    impl->SetCreateError(create_ec, HttpClientErrorStage::kCreate);
  }
  return CreateTask(std::move(impl));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateSessionTask(
    std::shared_ptr<SessionRequestAssembler> assembler, Executor io_executor,
    Executor callback_executor, std::string scheme, std::string host,
    std::string port, std::string target, http::verb method,
    HttpClientOptions options, SslContextPtr ssl_ctx,
    boost::system::error_code create_ec) {
  auto builder = DirectStreamBuilder::Create();

  const bool use_ssl = (scheme == "https");
  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), host, port, target,
      method, std::move(options), use_ssl, ssl_ctx);
  impl->SetAssembler(assembler, builder);
  if (create_ec) {
    impl->SetCreateError(create_ec, HttpClientErrorStage::kCreate);
  }
  return CreateTask(std::move(impl));
}

std::shared_ptr<HttpClientTask::Impl> HttpClientTask::CreateDefaultHttpsImpl(
    Executor io_executor, Executor callback_executor, std::string host,
    std::string port, std::string target, http::verb method,
    HttpClientOptions options) {
  const auto& ssl_state =
      connection_internal::GetDefaultClientSslContextState();
  auto impl =
      AllocateShared<Impl>(std::move(io_executor), std::move(callback_executor),
                           std::move(host), std::move(port), std::move(target),
                           method, std::move(options), true, ssl_state.ssl_ctx);
  if (ssl_state.ec) {
    impl->SetCreateError(ssl_state.ec, HttpClientErrorStage::kCreate);
  }
  return impl;
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateTask(
    std::shared_ptr<Impl> impl) {
  void* raw = Allocate(sizeof(HttpClientTask), alignof(HttpClientTask));
  try {
    auto* task = new (raw) HttpClientTask(std::move(impl));
    return {task, [](HttpClientTask* ptr) { DestroyDeallocate(ptr); }};
  } catch (...) {
    Deallocate(raw, sizeof(HttpClientTask), alignof(HttpClientTask));
    throw;
  }
}

// ---- Non-Raw factories: assembled mode ----

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttp(
    Executor io_executor, std::string host, std::string port,
    std::string target, http::verb method, HttpClientOptions options) {
  return CreateHttp(io_executor, io_executor, std::move(host), std::move(port),
                    std::move(target), method, std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttp(
    Executor io_executor, Executor callback_executor, std::string host,
    std::string port, std::string target, http::verb method,
    HttpClientOptions options) {
  return HttpClientTask::CreateAssembledTask(
      std::move(io_executor), std::move(callback_executor), "http",
      std::move(host), std::move(port), std::move(target), method,
      std::move(options), nullptr);
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttps(
    Executor io_executor, std::string host, std::string port,
    std::string target, http::verb method, HttpClientOptions options) {
  return CreateHttps(io_executor, io_executor, std::move(host), std::move(port),
                     std::move(target), method, std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttps(
    Executor io_executor, Executor callback_executor, std::string host,
    std::string port, std::string target, http::verb method,
    HttpClientOptions options) {
  const auto& ssl_state =
      connection_internal::GetDefaultClientSslContextState();
  return HttpClientTask::CreateAssembledTask(
      std::move(io_executor), std::move(callback_executor), "https",
      std::move(host), std::move(port), std::move(target), method,
      std::move(options), ssl_state.ssl_ctx, ssl_state.ec);
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttps(
    Executor io_executor, SslContextPtr ssl_ctx, std::string host,
    std::string port, std::string target, http::verb method,
    HttpClientOptions options) {
  return CreateHttps(io_executor, io_executor, std::move(ssl_ctx),
                     std::move(host), std::move(port), std::move(target),
                     method, std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttps(
    Executor io_executor, Executor callback_executor, SslContextPtr ssl_ctx,
    std::string host, std::string port, std::string target, http::verb method,
    HttpClientOptions options) {
  return HttpClientTask::CreateAssembledTask(
      std::move(io_executor), std::move(callback_executor), "https",
      std::move(host), std::move(port), std::move(target), method,
      std::move(options), std::move(ssl_ctx));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateFromUrl(
    Executor io_executor, const std::string& url, http::verb method,
    HttpClientOptions options) {
  return CreateFromUrl(io_executor, io_executor, url, method,
                       std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateFromUrl(
    Executor io_executor, Executor callback_executor, const std::string& url,
    http::verb method, HttpClientOptions options) {
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    auto impl = AllocateShared<Impl>(
        std::move(io_executor), std::move(callback_executor), "", "", "/",
        method, std::move(options), false, nullptr);
    impl->SetCreateError(make_error_code(boost::system::errc::invalid_argument),
                         HttpClientErrorStage::kCreate);
    return CreateTask(std::move(impl));
  }

  if (parsed->https) {
    const auto& ssl_state =
        connection_internal::GetDefaultClientSslContextState();
    return HttpClientTask::CreateAssembledTask(
        std::move(io_executor), std::move(callback_executor), "https",
        std::move(parsed->host), std::move(parsed->port),
        std::move(parsed->target), method, std::move(options),
        ssl_state.ssl_ctx, ssl_state.ec);
  }

  return HttpClientTask::CreateAssembledTask(
      std::move(io_executor), std::move(callback_executor), "http",
      std::move(parsed->host), std::move(parsed->port),
      std::move(parsed->target), method, std::move(options), nullptr);
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateFromUrl(
    Executor io_executor, SslContextPtr ssl_ctx, const std::string& url,
    http::verb method, HttpClientOptions options) {
  return CreateFromUrl(io_executor, io_executor, std::move(ssl_ctx), url,
                       method, std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateFromUrl(
    Executor io_executor, Executor callback_executor, SslContextPtr ssl_ctx,
    const std::string& url, http::verb method, HttpClientOptions options) {
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    auto impl = AllocateShared<Impl>(
        std::move(io_executor), std::move(callback_executor), "", "", "/",
        method, std::move(options), false, nullptr);
    impl->SetCreateError(make_error_code(boost::system::errc::invalid_argument),
                         HttpClientErrorStage::kCreate);
    return CreateTask(std::move(impl));
  }

  auto effective_ssl_ctx = parsed->https ? std::move(ssl_ctx) : SslContextPtr{};
  return HttpClientTask::CreateAssembledTask(
      std::move(io_executor), std::move(callback_executor),
      parsed->https ? "https" : "http", std::move(parsed->host),
      std::move(parsed->port), std::move(parsed->target), method,
      std::move(options), std::move(effective_ssl_ctx));
}

// ---- Raw factories: unchanged ----

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttpRaw(
    Executor io_executor, TcpStream stream, std::string host,
    std::string target, http::verb method, HttpClientOptions options) {
  return CreateHttpRaw(io_executor, io_executor, std::move(stream),
                       std::move(host), std::move(target), method,
                       std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttpRaw(
    Executor io_executor, Executor callback_executor, TcpStream stream,
    std::string host, std::string target, http::verb method,
    HttpClientOptions options) {
  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), std::move(host), "",
      std::move(target), method, std::move(options), false, nullptr);
  impl->SetRawTcpStream(std::move(stream));
  return CreateTask(std::move(impl));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttpsRaw(
    Executor io_executor, SslStream stream, std::string host,
    std::string target, http::verb method, HttpClientOptions options) {
  return CreateHttpsRaw(io_executor, io_executor, std::move(stream),
                        std::move(host), std::move(target), method,
                        std::move(options));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttpsRaw(
    Executor io_executor, Executor callback_executor, SslStream stream,
    std::string host, std::string target, http::verb method,
    HttpClientOptions options) {
  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), std::move(host), "",
      std::move(target), method, std::move(options), true, nullptr);
  impl->SetRawSslStream(std::move(stream));
  return CreateTask(std::move(impl));
}

// ---- Callback registration ----

std::shared_ptr<HttpClientTask> HttpClientTask::OnConnected(Callback cb) {
  impl_->SetOnConnected(std::move(cb));
  return shared_from_this();
}

std::shared_ptr<HttpClientTask> HttpClientTask::OnHeader(Callback cb) {
  impl_->SetOnHeader(std::move(cb));
  return shared_from_this();
}

std::shared_ptr<HttpClientTask> HttpClientTask::OnChunk(Callback cb) {
  impl_->SetOnChunk(std::move(cb));
  return shared_from_this();
}

std::shared_ptr<HttpClientTask> HttpClientTask::OnDone(Callback cb) {
  impl_->SetOnDone(std::move(cb));
  return shared_from_this();
}

HttpClientRequest& HttpClientTask::Request() noexcept {
  return impl_->Request();
}

JsonErrorCode HttpClientResult::ParseJsonBody(JsonValue& out) const {
  return ParseJsonText(response.body(), out);
}

JsonErrorCode HttpClientResult::ParseJsonBody(JsonObject& out) const {
  return ParseJsonText(response.body(), out);
}

bool HttpClientResult::TryParseJsonBody(JsonValue& out) const {
  const JsonErrorCode parse_ec = ParseJsonBody(out);
  return !parse_ec;
}

bool HttpClientResult::TryParseJsonBody(JsonObject& out) const {
  const JsonErrorCode parse_ec = ParseJsonBody(out);
  return !parse_ec;
}

void HttpClientTask::SetJson(const JsonValue& value) {
  SetJsonBody(Request(), value);
}

void HttpClientTask::SetJson(JsonValue&& value) {
  SetJson(static_cast<const JsonValue&>(value));
}

void HttpClientTask::Start() { impl_->Start(); }

void HttpClientTask::Cancel() { impl_->Cancel(); }

bool HttpClientTask::Failed() const noexcept { return impl_->Failed(); }

boost::system::error_code HttpClientTask::ErrorCode() const noexcept {
  return impl_->ErrorCode();
}

HttpClientErrorStage HttpClientTask::ErrorStage() const noexcept {
  return impl_->ErrorStage();
}

}  // namespace bsrvcore
