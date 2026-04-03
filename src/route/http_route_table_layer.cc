/**
 * @file http_route_table_layer.cc
 * @brief HttpRouteTableLayer implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-25
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements a routing tree node that stores handlers, aspects, and children.
 */

#include "internal/http_route_table_layer.h"

#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"

class HttpRequestHandler;

using namespace bsrvcore::route_internal;

HttpRouteTableLayer::HttpRouteTableLayer()
    : default_route_(nullptr), handler_(nullptr), route_template_("/") {}

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
    OwnedPtr<HttpRequestHandler> handler) noexcept {
  if (handler == nullptr) {
    return false;
  }

  handler_ = std::move(handler);

  return true;
}

bool HttpRouteTableLayer::SetDefaultRoute(
    OwnedPtr<HttpRouteTableLayer> route) noexcept {
  if (route == nullptr) {
    return false;
  }

  default_route_ = std::move(route);

  return true;
}

bool HttpRouteTableLayer::SetRoute(std::string key,
                                   OwnedPtr<HttpRouteTableLayer> link) try {
  if (key.empty()) {
    return false;
  }

  if (link == nullptr) {
    return false;
  }

  if (map_.contains(key) != 0u) {
    map_.at(key) = std::move(link);
  } else {
    map_.emplace(std::move(key), std::move(link));
  }

  return true;
} catch (const std::exception&) {
  return false;
}

void HttpRouteTableLayer::SetIgnoreDefaultRoute(bool flag) noexcept {
  ignore_default_route_ = flag;
}

HttpRouteTableLayer* HttpRouteTableLayer::GetDefaultRoute() const noexcept {
  return default_route_.get();
}

HttpRouteTableLayer* HttpRouteTableLayer::GetRoute(
    const std::string& key) const noexcept {
  auto it = map_.find(key);
  if (it == map_.end()) {
    return nullptr;
  }
  return it->second.get();
}

HttpRouteTableLayer* HttpRouteTableLayer::GetRoute(
    std::string&& key) const noexcept {
  auto it = map_.find(key);
  if (it == map_.end()) {
    return nullptr;
  }
  return it->second.get();
}

bsrvcore::HttpRequestHandler* HttpRouteTableLayer::GetHandler() noexcept {
  return handler_.get();
}

void HttpRouteTableLayer::SetParamNames(
    std::vector<std::string> param_names) noexcept {
  param_names_ = std::move(param_names);
}

const std::vector<std::string>& HttpRouteTableLayer::GetParamNames()
    const noexcept {
  return param_names_;
}

void HttpRouteTableLayer::SetRouteTemplate(
    std::string route_template) noexcept {
  route_template_ = std::move(route_template);
}

const std::string& HttpRouteTableLayer::GetRouteTemplate() const noexcept {
  return route_template_;
}

bool HttpRouteTableLayer::GetIgnoreDefaultRoute() const noexcept {
  return ignore_default_route_;
}

bool HttpRouteTableLayer::AddAspect(
    OwnedPtr<HttpRequestAspectHandler> aspect) try {
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
