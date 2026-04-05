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
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>

#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/internal/session/session_map.h"

using namespace bsrvcore;

HttpServer* HttpServer::AddListen(boost::asio::ip::tcp::endpoint ep,
                                  std::size_t io_threads) {
  return AddListen(std::move(ep), io_threads, nullptr);
}

HttpServer* HttpServer::AddListen(
    boost::asio::ip::tcp::endpoint ep, std::size_t io_threads,
    std::shared_ptr<boost::asio::ssl::context> ssl_ctx) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  if (io_threads == 0) {
    io_threads = 1;
  }

  endpoint_configs_.push_back(
      EndpointListenConfig{.endpoint = std::move(ep),
                           .io_threads = io_threads,
                           .ssl_ctx = std::move(ssl_ctx)});
  return this;
}

HttpServer* HttpServer::SetHeaderReadExpiry(std::size_t expiry) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  header_read_expiry_ = expiry;
  return this;
}

HttpServer* HttpServer::SetKeepAliveTimeout(std::size_t timeout) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  keep_alive_timeout_ = timeout;
  return this;
}

HttpServer* HttpServer::SetLogger(std::shared_ptr<Logger> logger) {
  std::scoped_lock const lock(mtx_);
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
