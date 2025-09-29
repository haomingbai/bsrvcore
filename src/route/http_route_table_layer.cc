/**
 * @file http_route_table.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-25
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/internal/http_route_table_layer.h"

#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/http_request_handler.h"

namespace bsrvcore {

class HttpRequestHandler;

using namespace route_internal;

HttpRouteTableLayer::HttpRouteTableLayer()
    : default_route_(nullptr),
      handler_(nullptr),
      max_body_size_(0),
      read_expiry_(0),
      write_expiry_(0),
      ignore_default_route_(false) {}

void HttpRouteTableLayer::SetMaxBodySize(std::size_t max_body_size) noexcept {
  max_body_size_ = max_body_size;
}

void HttpRouteTableLayer::SetReadExpiry(std::size_t expiry) noexcept {
  read_expiry_ = expiry;
}

void HttpRouteTableLayer::SetWriteExpiry(std::size_t expiry) noexcept {
  write_expiry_ = expiry;
}

bool HttpRouteTableLayer::SetHandler(
    std::unique_ptr<HttpRequestHandler> handler) noexcept {
  if (handler == nullptr) {
    return false;
  }

  handler_ = std::move(handler);

  return true;
}

bool HttpRouteTableLayer::SetDefaultRoute(
    std::unique_ptr<HttpRouteTableLayer> route) noexcept {
  if (route == nullptr) {
    return false;
  }

  default_route_ = std::move(route);

  return true;
}

bool HttpRouteTableLayer::SetRoute(
    std::string key, std::unique_ptr<HttpRouteTableLayer> link) try {
  if (key.empty()) {
    return false;
  }

  if (link == nullptr) {
    return false;
  }

  if (map_.count(key)) {
    map_.at(key) = std::move(link);
  } else {
    map_.emplace(std::move(key), std::move(link));
  }

  return true;
} catch (const std::exception& e) {
  return false;
}

void HttpRouteTableLayer::SetIgnoreDefaultRoute(bool flag) noexcept {
  if (flag) {
    ignore_default_route_ = true;
  } else {
    ignore_default_route_ = false;
  }
}

HttpRouteTableLayer* HttpRouteTableLayer::GetDefaultRoute() noexcept {
  return default_route_.get();
}

HttpRouteTableLayer* HttpRouteTableLayer::GetRoute(
    const std::string& key) noexcept {
  if (map_.count(key)) {
    return map_[key].get();
  } else {
    return nullptr;
  }
}

HttpRequestHandler* HttpRouteTableLayer::GetHandler() noexcept {
  return handler_.get();
}

bool HttpRouteTableLayer::GetIgnoreDefaultRoute() noexcept {
  return ignore_default_route_;
}

}  // namespace bsrvcore
