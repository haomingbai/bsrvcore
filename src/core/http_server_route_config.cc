/**
 * @file http_server_route_config.cc
 * @brief Route and policy configuration methods for HttpServer.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <cstddef>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/blue_print.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/internal/route/computing_route_handler.h"
#include "bsrvcore/internal/route/http_route_table.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"

using namespace bsrvcore;

HttpServer* HttpServer::AddRouteEntry(HttpRequestMethod method,
                                      const std::string_view url,
                                      OwnedPtr<HttpRequestHandler> handler) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->AddRouteEntry(method, url, std::move(handler));
  return this;
}

HttpServer* HttpServer::AddComputingRouteEntry(
    HttpRequestMethod method, const std::string_view url,
    OwnedPtr<HttpRequestHandler> handler) {
  return AddRouteEntry(
      method, url, route_internal::WrapComputingHandler(std::move(handler)));
}

HttpServer* HttpServer::AddExclusiveRouteEntry(
    HttpRequestMethod method, const std::string_view url,
    OwnedPtr<HttpRequestHandler> handler) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->AddExclusiveRouteEntry(method, url, std::move(handler));
  return this;
}

HttpServer* HttpServer::AddAspect(HttpRequestMethod method,
                                  const std::string_view url,
                                  OwnedPtr<HttpRequestAspectHandler> aspect) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->AddAspect(method, url, std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddTerminalAspect(
    HttpRequestMethod method, const std::string_view url,
    OwnedPtr<HttpRequestAspectHandler> aspect) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->AddTerminalAspect(method, url, std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddGlobalAspect(
    HttpRequestMethod method, OwnedPtr<HttpRequestAspectHandler> aspect) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->AddGlobalAspect(method, std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddGlobalAspect(
    OwnedPtr<HttpRequestAspectHandler> aspect) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->AddGlobalAspect(std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddBluePrint(std::string_view prefix,
                                     BluePrint&& blue_print) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  std::move(blue_print).MountInto(prefix, *route_table_);
  return this;
}

HttpServer* HttpServer::AddBluePrint(std::string_view prefix,
                                     const ReuseableBluePrint& blue_print) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  blue_print.MountInto(prefix, *route_table_);
  return this;
}

HttpServer* HttpServer::SetReadExpiry(HttpRequestMethod method,
                                      std::string_view url,
                                      std::size_t expiry) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->SetReadExpiry(method, url, expiry);
  return this;
}

HttpServer* HttpServer::SetWriteExpiry(HttpRequestMethod method,
                                       std::string_view url,
                                       std::size_t expiry) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->SetWriteExpiry(method, url, expiry);
  return this;
}

HttpServer* HttpServer::SetMaxBodySize(HttpRequestMethod method,
                                       std::string_view url, std::size_t size) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->SetMaxBodySize(method, url, size);
  return this;
}

HttpServer* HttpServer::SetDefaultReadExpiry(std::size_t expiry) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultReadExpiry(expiry);
  return this;
}

HttpServer* HttpServer::SetDefaultWriteExpiry(std::size_t expiry) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultWriteExpiry(expiry);
  return this;
}

HttpServer* HttpServer::SetDefaultMaxBodySize(std::size_t size) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultMaxBodySize(size);
  return this;
}

HttpServer* HttpServer::SetDefaultHandler(
    OwnedPtr<HttpRequestHandler> handler) {
  std::scoped_lock const lock(mtx_);
  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultHandler(std::move(handler));
  return this;
}
