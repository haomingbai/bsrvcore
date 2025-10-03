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
#include <vector>

#include "bsrvcore/http_request_aspect_handler.h"
#include "bsrvcore/http_request_handler.h"

class HttpRequestHandler;

using namespace bsrvcore::route_internal;

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

HttpRouteTableLayer* HttpRouteTableLayer::GetRoute(std::string&& key) noexcept {
  if (map_.count(key)) {
    return map_[key].get();
  } else {
    return nullptr;
  }
}

bsrvcore::HttpRequestHandler* HttpRouteTableLayer::GetHandler() noexcept {
  return handler_.get();
}

bool HttpRouteTableLayer::GetIgnoreDefaultRoute() noexcept {
  return ignore_default_route_;
}

bool HttpRouteTableLayer::AddAspect(
    std::unique_ptr<HttpRequestAspectHandler> aspect) try {
  aspects_.emplace_back(std::move(aspect));
  return true;
} catch (...) {
  return false;
}

std::vector<bsrvcore::HttpRequestAspectHandler*>
HttpRouteTableLayer::GetAspects() const {
  std::vector<bsrvcore::HttpRequestAspectHandler*> aspects(aspects_.size());
  for (std::size_t i = 0; i < aspects_.size(); i++) {
    aspects[i] = aspects_[i].get();
  }

  return aspects;
}

std::size_t HttpRouteTableLayer::GetAspectNum() const noexcept {
  return aspects_.size();
}

std::size_t HttpRouteTableLayer::GetMaxBodySize() const noexcept {
  return max_body_size_;
}

std::size_t HttpRouteTableLayer::GetReadExpiry() const noexcept {
  return read_expiry_;
}

std::size_t HttpRouteTableLayer::GetWriteExpiry() const noexcept {
  return write_expiry_;
}
