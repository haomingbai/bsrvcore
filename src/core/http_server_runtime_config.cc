/**
 * @file http_server_runtime_config.cc
 * @brief Runtime resource configuration methods for HttpServer.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <memory>
#include <shared_mutex>
#include <utility>

#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/internal/session/session_map.h"

using namespace bsrvcore;

HttpServer* HttpServer::AddListen(boost::asio::ip::tcp::endpoint ep) {
  std::shared_lock<std::shared_mutex> lock(mtx_);
  if (is_running_) {
    return this;
  }

  acceptors_.emplace_back(ioc_, ep);
  return this;
}

HttpServer* HttpServer::SetHeaderReadExpiry(std::size_t expiry) {
  std::shared_lock<std::shared_mutex> lock(mtx_);
  if (is_running_) {
    return this;
  }

  header_read_expiry_ = expiry;
  return this;
}

HttpServer* HttpServer::SetKeepAliveTimeout(std::size_t timeout) {
  std::shared_lock<std::shared_mutex> lock(mtx_);
  if (is_running_) {
    return this;
  }

  keep_alive_timeout_ = timeout;
  return this;
}

HttpServer* HttpServer::SetSslContext(boost::asio::ssl::context ctx) {
  std::shared_lock<std::shared_mutex> lock(mtx_);
  if (is_running_) {
    return this;
  }

  ssl_ctx_ = std::move(ctx);
  return this;
}

HttpServer* HttpServer::UnsetSslContext() {
  std::shared_lock<std::shared_mutex> lock(mtx_);
  if (is_running_) {
    return this;
  }

  ssl_ctx_.reset();
  return this;
}

HttpServer* HttpServer::SetLogger(std::shared_ptr<Logger> logger) {
  std::shared_lock<std::shared_mutex> lock(mtx_);
  if (is_running_) {
    return this;
  }

  logger_ = std::move(logger);
  return this;
}

HttpServer* HttpServer::SetDefaultSessionTimeout(std::size_t timeout) {
  sessions_->SetDefaultSessionTimeout(timeout);
  return this;
}

HttpServer* HttpServer::SetSessionCleaner(bool use_cleaner) {
  sessions_->SetBackgroundCleaner(use_cleaner);
  return this;
}
