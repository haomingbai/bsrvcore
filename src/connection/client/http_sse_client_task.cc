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

std::shared_ptr<HttpSseClientTask::Impl>
HttpSseClientTask::CreateDefaultHttpsImpl(Executor io_executor,
                                          Executor callback_executor,
                                          std::string host, std::string port,
                                          std::string target,
                                          HttpSseClientOptions options) {
  const auto& ssl_state =
      connection_internal::GetDefaultClientSslContextState();
  auto impl =
      AllocateShared<Impl>(std::move(io_executor), std::move(callback_executor),
                           std::move(host), std::move(port), std::move(target),
                           std::move(options), true, ssl_state.ssl_ctx);
  if (ssl_state.ec) {
    impl->SetCreateError(ssl_state.ec, HttpSseClientErrorStage::kCreate);
  }
  return impl;
}

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
  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), std::move(host),
      std::move(port), std::move(target), std::move(options), false, nullptr);
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
  auto impl = CreateDefaultHttpsImpl(
      std::move(io_executor), std::move(callback_executor), std::move(host),
      std::move(port), std::move(target), std::move(options));
  return CreateTask(std::move(impl));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttps(
    Executor io_executor, std::shared_ptr<boost::asio::ssl::context> ssl_ctx,
    std::string host, std::string port, std::string target,
    HttpSseClientOptions options) {
  return CreateHttps(io_executor, io_executor, std::move(ssl_ctx),
                     std::move(host), std::move(port), std::move(target),
                     std::move(options));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttps(
    Executor io_executor, Executor callback_executor,
    std::shared_ptr<boost::asio::ssl::context> ssl_ctx, std::string host,
    std::string port, std::string target, HttpSseClientOptions options) {
  auto impl =
      AllocateShared<Impl>(std::move(io_executor), std::move(callback_executor),
                           std::move(host), std::move(port), std::move(target),
                           std::move(options), true, std::move(ssl_ctx));
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
    auto impl = CreateDefaultHttpsImpl(
        std::move(io_executor), std::move(callback_executor), parsed->host,
        parsed->port, parsed->target, std::move(options));
    return CreateTask(std::move(impl));
  }

  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), parsed->host,
      parsed->port, parsed->target, std::move(options), false, nullptr);
  return CreateTask(std::move(impl));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateFromUrl(
    Executor io_executor, std::shared_ptr<boost::asio::ssl::context> ssl_ctx,
    const std::string& url, HttpSseClientOptions options) {
  return CreateFromUrl(io_executor, io_executor, std::move(ssl_ctx), url,
                       std::move(options));
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateFromUrl(
    Executor io_executor, Executor callback_executor,
    std::shared_ptr<boost::asio::ssl::context> ssl_ctx, const std::string& url,
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

  auto effective_ssl_ctx = parsed->https
                               ? std::move(ssl_ctx)
                               : std::shared_ptr<boost::asio::ssl::context>{};
  auto impl = AllocateShared<Impl>(
      std::move(io_executor), std::move(callback_executor), parsed->host,
      parsed->port, parsed->target, std::move(options), parsed->https,
      std::move(effective_ssl_ctx));
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
