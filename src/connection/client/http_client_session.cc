/**
 * @file http_client_session.cc
 * @brief HttpClientSession task factory implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/http_client_session.h"

#include <string>
#include <utility>

namespace bsrvcore {

std::shared_ptr<HttpClientTask> HttpClientSession::CreateHttp(
    boost::asio::any_io_executor executor, std::string host, std::string port,
    std::string target, boost::beast::http::verb method,
    HttpClientOptions options) {
  auto task = HttpClientTask::CreateHttp(std::move(executor), std::move(host),
                                         std::move(port), std::move(target),
                                         method, std::move(options));
  task->AttachSession(weak_from_this());
  return task;
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateHttps(
    boost::asio::any_io_executor executor, boost::asio::ssl::context& ssl_ctx,
    std::string host, std::string port, std::string target,
    boost::beast::http::verb method, HttpClientOptions options) {
  auto task = HttpClientTask::CreateHttps(
      std::move(executor), ssl_ctx, std::move(host), std::move(port),
      std::move(target), method, std::move(options));
  task->AttachSession(weak_from_this());
  return task;
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateFromUrl(
    boost::asio::any_io_executor executor, std::string url,
    boost::beast::http::verb method, HttpClientOptions options) {
  auto task = HttpClientTask::CreateFromUrl(std::move(executor), std::move(url),
                                            method, std::move(options));
  task->AttachSession(weak_from_this());
  return task;
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateFromUrl(
    boost::asio::any_io_executor executor, boost::asio::ssl::context& ssl_ctx,
    std::string url, boost::beast::http::verb method,
    HttpClientOptions options) {
  auto task = HttpClientTask::CreateFromUrl(
      std::move(executor), ssl_ctx, std::move(url), method, std::move(options));
  task->AttachSession(weak_from_this());
  return task;
}
}  // namespace bsrvcore
