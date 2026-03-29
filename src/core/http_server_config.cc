/**
 * @file http_server_config.cc
 * @brief HttpServer configuration API implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements configuration methods such as routes, aspects, timeouts and
 * limits.
 */

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <string_view>
#include <utility>

#include "bsrvcore/core/blue_print.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/internal/route/computing_route_handler.h"
#include "bsrvcore/internal/route/http_route_table.h"
#include "bsrvcore/internal/session/session_map.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"

using namespace bsrvcore;

HttpServer* HttpServer::AddRouteEntry(HttpRequestMethod method,
                                      const std::string_view url,
                                      OwnedPtr<HttpRequestHandler> handler) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddRouteEntry(method, url, std::move(handler));
  return this;
}

HttpServer* HttpServer::AddComputingRouteEntry(
    HttpRequestMethod method, const std::string_view url,
    OwnedPtr<HttpRequestHandler> handler) {
  return AddRouteEntry(method, url,
                       route_internal::WrapComputingHandler(
                           std::move(handler)));
}

HttpServer* HttpServer::AddExclusiveRouteEntry(
    HttpRequestMethod method, const std::string_view url,
    OwnedPtr<HttpRequestHandler> handler) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddExclusiveRouteEntry(method, url, std::move(handler));
  return this;
}

HttpServer* HttpServer::AddAspect(HttpRequestMethod method,
                                  const std::string_view url,
                                  OwnedPtr<HttpRequestAspectHandler> aspect) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddAspect(method, url, std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddGlobalAspect(
    HttpRequestMethod method, OwnedPtr<HttpRequestAspectHandler> aspect) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddGlobalAspect(method, std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddGlobalAspect(
    OwnedPtr<HttpRequestAspectHandler> aspect) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddGlobalAspect(std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddBluePrint(std::string_view prefix,
                                     BluePrint&& blue_print) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  std::move(blue_print).MountInto(prefix, *route_table_);
  return this;
}

HttpServer* HttpServer::AddBluePrint(std::string_view prefix,
                                     const ReuseableBluePrint& blue_print) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  blue_print.MountInto(prefix, *route_table_);
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
    OwnedPtr<HttpRequestHandler> handler) {
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

HttpServer* HttpServer::SetDefaultSessionTimeout(std::size_t timeout) {
  sessions_->SetDefaultSessionTimeout(timeout);
  return this;
}

HttpServer* HttpServer::SetSessionCleaner(bool use_cleaner) {
  sessions_->SetBackgroundCleaner(use_cleaner);
  return this;
}
