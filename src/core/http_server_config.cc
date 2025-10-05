/**
 * @file http_server_config.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/http_server.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/detail/chrono.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <cstddef>
#include <cstring>
#include <memory>
#include <shared_mutex>
#include <string_view>
#include <utility>

#include "bsrvcore/http_request_aspect_handler.h"
#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_route_result.h"
#include "bsrvcore/internal/empty_logger.h"
#include "bsrvcore/internal/http_route_table.h"
#include "bsrvcore/internal/http_server_connection_impl.h"
#include "bsrvcore/internal/session_map.h"
#include "bsrvcore/logger.h"

using namespace bsrvcore;

HttpServer* HttpServer::AddRouteEntry(
    HttpRequestMethod method, const std::string_view url,
    std::unique_ptr<HttpRequestHandler> handler) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddRouteEntry(method, url, std::move(handler));
  return this;
}

HttpServer* HttpServer::AddExclusiveRouteEntry(
    HttpRequestMethod method, const std::string_view url,
    std::unique_ptr<HttpRequestHandler> handler) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddExclusiveRouteEntry(method, url, std::move(handler));
  return this;
}

HttpServer* HttpServer::AddAspect(
    HttpRequestMethod method, const std::string_view url,
    std::unique_ptr<HttpRequestAspectHandler> aspect) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddAspect(method, url, std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddGlobalAspect(
    HttpRequestMethod method,
    std::unique_ptr<HttpRequestAspectHandler> aspect) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddGlobalAspect(method, std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddGlobalAspect(
    std::unique_ptr<HttpRequestAspectHandler> aspect) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddGlobalAspect(std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddListen(boost::asio::ip::tcp::endpoint ep) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  acceptors_.emplace_back(ioc_, ep);
  return this;
}

HttpServer* HttpServer::SetReadExpiry(HttpRequestMethod method,
                                      std::string_view url,
                                      std::size_t expiry) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetReadExpiry(method, url, expiry);
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

HttpServer* HttpServer::SetWriteExpiry(HttpRequestMethod method,
                                       std::string_view url,
                                       std::size_t expiry) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetWriteExpiry(method, url, expiry);
  return this;
}

HttpServer* HttpServer::SetMaxBodySize(HttpRequestMethod method,
                                       std::string_view url, std::size_t size) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetMaxBodySize(method, url, size);
  return this;
}

HttpServer* HttpServer::SetDefaultReadExpiry(std::size_t expiry) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultReadExpiry(expiry);
  return this;
}

HttpServer* HttpServer::SetDefaultWriteExpiry(std::size_t expiry) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultWriteExpiry(expiry);
  return this;
}

HttpServer* HttpServer::SetDefaultMaxBodySize(std::size_t size) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultMaxBodySize(size);
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

HttpServer* HttpServer::SetDefaultHandler(
    std::unique_ptr<HttpRequestHandler> handler) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultHandler(std::move(handler));
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

  logger_ = logger;
  return this;
}
