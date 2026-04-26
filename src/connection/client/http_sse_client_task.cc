/**
 * @file http_sse_client_task.cc
 * @brief Public factory and forwarding layer for HttpSseClientTask.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/client/http_sse_client_task.h"

#include <boost/asio/ssl/context.hpp>
#include <boost/system/errc.hpp>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/stream_builder.h"
#include "impl/default_client_ssl_context.h"
#include "impl/http_sse_client_task_impl.h"
#include "impl/http_url_parser.h"

namespace bsrvcore {

namespace {

using connection_internal::ParseHttpUrl;

}  // namespace

HttpSseClientTask::HttpSseClientTask(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

HttpSseClientTask::~HttpSseClientTask() = default;

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateTask(
    std::shared_ptr<Impl> impl) {
  void* raw = Allocate(sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
  try {
    auto* task = new (raw) HttpSseClientTask(std::move(impl));
    return {task, [](HttpSseClientTask* ptr) { DestroyDeallocate(ptr); }};
  } catch (...) {
    Deallocate(raw, sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
    throw;
  }
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttp(
    Executor io_executor, std::string host, std::string port,
    std::string target, HttpSseClientOptions options) {
  return CreateHttp(io_executor, io_executor, std::move(host), std::move(port),
                    std::move(target), std::move(options));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttp(
    Executor io_executor, Executor callback_executor, std::string host,
    std::string port, std::string target, HttpSseClientOptions options) {
  // Create assembler BEFORE moving host/port into the impl.
  auto assembler = AllocateShared<DefaultRequestAssembler>("http", host, port);
  auto builder = DirectStreamBuilder::Create();

  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), std::move(host),
      std::move(port), std::move(target), std::move(options), false, nullptr);
  impl->SetAssembler(assembler, builder);

  return CreateTask(std::move(impl));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttps(
    Executor io_executor, std::string host, std::string port,
    std::string target, HttpSseClientOptions options) {
  return CreateHttps(io_executor, io_executor, std::move(host), std::move(port),
                     std::move(target), std::move(options));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttps(
    Executor io_executor, Executor callback_executor, std::string host,
    std::string port, std::string target, HttpSseClientOptions options) {
  const auto& ssl_state =
      connection_internal::GetDefaultClientSslContextState();

  // Create assembler BEFORE moving host/port into the impl.
  auto assembler = AllocateShared<DefaultRequestAssembler>("https", host, port,
                                                           ssl_state.ssl_ctx);
  auto builder = DirectStreamBuilder::Create();

  auto impl =
      AllocateShared<Impl>(std::move(io_executor), std::move(callback_executor),
                           std::move(host), std::move(port), std::move(target),
                           std::move(options), true, ssl_state.ssl_ctx);
  if (ssl_state.ec) {
    impl->SetCreateError(ssl_state.ec, HttpSseClientErrorStage::kCreate);
  }
  impl->SetAssembler(assembler, builder);

  return CreateTask(std::move(impl));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttps(
    Executor io_executor, SslContextPtr ssl_ctx, std::string host,
    std::string port, std::string target, HttpSseClientOptions options) {
  return CreateHttps(io_executor, io_executor, std::move(ssl_ctx),
                     std::move(host), std::move(port), std::move(target),
                     std::move(options));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttps(
    Executor io_executor, Executor callback_executor, SslContextPtr ssl_ctx,
    std::string host, std::string port, std::string target,
    HttpSseClientOptions options) {
  // Create assembler BEFORE moving host/port/ssl_ctx into the impl.
  auto assembler =
      AllocateShared<DefaultRequestAssembler>("https", host, port, ssl_ctx);
  auto builder = DirectStreamBuilder::Create();

  auto impl =
      AllocateShared<Impl>(std::move(io_executor), std::move(callback_executor),
                           std::move(host), std::move(port), std::move(target),
                           std::move(options), true, std::move(ssl_ctx));
  impl->SetAssembler(assembler, builder);

  return CreateTask(std::move(impl));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateFromUrl(
    Executor io_executor, const std::string& url,
    HttpSseClientOptions options) {
  return CreateFromUrl(io_executor, io_executor, url, std::move(options));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateFromUrl(
    Executor io_executor, Executor callback_executor, const std::string& url,
    HttpSseClientOptions options) {
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    auto impl = AllocateShared<Impl>(std::move(io_executor),
                                     std::move(callback_executor), "", "", "/",
                                     std::move(options), false, nullptr);
    impl->SetCreateError(make_error_code(boost::system::errc::invalid_argument),
                         HttpSseClientErrorStage::kCreate);
    return CreateTask(std::move(impl));
  }

  if (parsed->https) {
    const auto& ssl_state =
        connection_internal::GetDefaultClientSslContextState();
    auto impl = AllocateShared<Impl>(
        std::move(io_executor), std::move(callback_executor), parsed->host,
        parsed->port, parsed->target, std::move(options), true,
        ssl_state.ssl_ctx);
    if (ssl_state.ec) {
      impl->SetCreateError(ssl_state.ec, HttpSseClientErrorStage::kCreate);
    }

    auto assembler = AllocateShared<DefaultRequestAssembler>(
        "https", parsed->host, parsed->port, ssl_state.ssl_ctx);
    auto builder = DirectStreamBuilder::Create();
    impl->SetAssembler(assembler, builder);

    return CreateTask(std::move(impl));
  }

  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), parsed->host,
      parsed->port, parsed->target, std::move(options), false, nullptr);

  auto assembler = AllocateShared<DefaultRequestAssembler>("http", parsed->host,
                                                           parsed->port);
  auto builder = DirectStreamBuilder::Create();
  impl->SetAssembler(assembler, builder);

  return CreateTask(std::move(impl));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateFromUrl(
    Executor io_executor, SslContextPtr ssl_ctx, const std::string& url,
    HttpSseClientOptions options) {
  return CreateFromUrl(io_executor, io_executor, std::move(ssl_ctx), url,
                       std::move(options));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateFromUrl(
    Executor io_executor, Executor callback_executor, SslContextPtr ssl_ctx,
    const std::string& url, HttpSseClientOptions options) {
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    auto impl = AllocateShared<Impl>(std::move(io_executor),
                                     std::move(callback_executor), "", "", "/",
                                     std::move(options), false, nullptr);
    impl->SetCreateError(make_error_code(boost::system::errc::invalid_argument),
                         HttpSseClientErrorStage::kCreate);
    return CreateTask(std::move(impl));
  }

  auto effective_ssl_ctx = parsed->https ? std::move(ssl_ctx) : SslContextPtr{};
  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), parsed->host,
      parsed->port, parsed->target, std::move(options), parsed->https,
      effective_ssl_ctx);

  // Assembled mode: attach DefaultRequestAssembler + DirectStreamBuilder.
  auto assembler = AllocateShared<DefaultRequestAssembler>(
      parsed->https ? "https" : "http", parsed->host, parsed->port,
      effective_ssl_ctx);
  auto builder = DirectStreamBuilder::Create();
  impl->SetAssembler(assembler, builder);

  return CreateTask(std::move(impl));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttpRaw(
    Executor io_executor, TcpStream stream, std::string host,
    std::string target, HttpSseClientOptions options) {
  return CreateHttpRaw(io_executor, io_executor, std::move(stream),
                       std::move(host), std::move(target), std::move(options));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttpRaw(
    Executor io_executor, Executor callback_executor, TcpStream stream,
    std::string host, std::string target, HttpSseClientOptions options) {
  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), std::move(host), "",
      std::move(target), std::move(options), false, nullptr);
  impl->SetRawTcpStream(std::move(stream));
  // Raw mode: no assembler/builder attached.
  return CreateTask(std::move(impl));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttpsRaw(
    Executor io_executor, SslStream stream, std::string host,
    std::string target, HttpSseClientOptions options) {
  return CreateHttpsRaw(io_executor, io_executor, std::move(stream),
                        std::move(host), std::move(target), std::move(options));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttpsRaw(
    Executor io_executor, Executor callback_executor, SslStream stream,
    std::string host, std::string target, HttpSseClientOptions options) {
  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), std::move(host), "",
      std::move(target), std::move(options), true, nullptr);
  impl->SetRawSslStream(std::move(stream));
  // Raw mode: no assembler/builder attached.
  return CreateTask(std::move(impl));
}

HttpRequest& HttpSseClientTask::Request() noexcept { return impl_->Request(); }

void HttpSseClientTask::Start(Callback cb) { impl_->Start(std::move(cb)); }

void HttpSseClientTask::Next(Callback cb) { impl_->Next(std::move(cb)); }

void HttpSseClientTask::Cancel() { impl_->Cancel(); }

bool HttpSseClientTask::Failed() const noexcept { return impl_->Failed(); }

boost::system::error_code HttpSseClientTask::ErrorCode() const noexcept {
  return impl_->ErrorCode();
}

HttpSseClientErrorStage HttpSseClientTask::ErrorStage() const noexcept {
  return impl_->ErrorStage();
}

}  // namespace bsrvcore
