/**
 * @file http_route_table_registry.cc
 * @brief Route registration and policy storage for HttpRouteTable.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/internal/route/empty_route_handler.h"
#include "bsrvcore/internal/route/http_route_table.h"
#include "bsrvcore/internal/route/http_route_table_detail.h"
#include "bsrvcore/internal/route/http_route_table_layer.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"
#include "impl/http_route_target_validator.h"

using bsrvcore::HttpRequestAspectHandler;
using bsrvcore::HttpRequestHandler;
using bsrvcore::HttpRequestMethod;
using bsrvcore::HttpRouteTable;
using bsrvcore::route_internal::HttpRouteTableLayer;

namespace bsrvcore {

bool HttpRouteTable::AddRouteEntry(HttpRequestMethod method,
                                   const std::string_view target,
                                   OwnedPtr<HttpRequestHandler> handler) {
  auto method_idx = static_cast<std::size_t>(method);
  if (method_idx >= kHttpRequestMethodNum ||
      !route_internal::IsValidParametricTarget(target)) {
    return false;
  }

  auto* route_layer = GetOrCreateRouteTableLayer(method, target);
  if (route_layer == nullptr) {
    return false;
  }

  route_layer->SetHandler(std::move(handler));
  return true;
}

bool HttpRouteTable::AddExclusiveRouteEntry(
    HttpRequestMethod method, const std::string_view target,
    OwnedPtr<HttpRequestHandler> handler) {
  auto method_idx = static_cast<std::size_t>(method);
  if (method_idx >= kHttpRequestMethodNum ||
      !route_internal::IsValidParametricTarget(target)) {
    return false;
  }

  auto* route_layer = GetOrCreateRouteTableLayer(method, target);
  if (route_layer == nullptr) {
    return false;
  }

  route_layer->SetHandler(std::move(handler));
  route_layer->SetIgnoreDefaultRoute(true);
  return true;
}

HttpRouteTableLayer* HttpRouteTable::GetOrCreateRouteTableLayer(
    HttpRequestMethod method, const std::string_view target) {
  auto* route_layer = entrance_[static_cast<std::size_t>(method)].get();
  for (auto word : route_internal::detail::SplitTargetSegments(target)) {
    if (route_internal::detail::IsParameterSegment(word)) {
      // Parameter segments all share the dedicated default_route_ edge. This
      // keeps the tree compact and lets matching choose "exact segment first,
      // wildcard second" at each depth.
      if (HttpRouteTableLayer* next_layer = route_layer->GetDefaultRoute()) {
        route_layer = next_layer;
      } else {
        auto new_layer = AllocateUnique<HttpRouteTableLayer>();
        auto* created_layer = new_layer.get();
        route_layer->SetDefaultRoute(std::move(new_layer));
        route_layer = created_layer;
      }
      continue;
    }

    if (auto* next_layer = route_layer->GetRoute(std::string(word))) {
      route_layer = next_layer;
    } else {
      auto new_layer = AllocateUnique<HttpRouteTableLayer>();
      auto* created_layer = new_layer.get();
      route_layer->SetRoute(std::string(word), std::move(new_layer));
      route_layer = created_layer;
    }
  }

  // Terminal metadata is rewritten on repeated registration so policies and
  // parameter names always reflect the latest route definition for this path.
  route_layer->SetParamNames(ExtractParamNames(target));
  route_layer->SetRouteTemplate(NormalizeRouteTemplate(target));
  return route_layer;
}

bool HttpRouteTable::AddAspect(HttpRequestMethod method,
                               const std::string_view target,
                               OwnedPtr<HttpRequestAspectHandler> aspect) {
  auto method_idx = static_cast<std::size_t>(method);
  if (method_idx >= kHttpRequestMethodNum ||
      !route_internal::IsValidParametricTarget(target)) {
    return false;
  }

  auto* route_layer = GetOrCreateRouteTableLayer(method, target);
  if (route_layer == nullptr) {
    return false;
  }

  route_layer->AddAspect(std::move(aspect));
  return true;
}

bool HttpRouteTable::AddGlobalAspect(
    HttpRequestMethod method, OwnedPtr<HttpRequestAspectHandler> aspect) try {
  auto method_idx = static_cast<std::size_t>(method);
  if (method_idx >= kHttpRequestMethodNum) {
    return false;
  }
  global_specific_aspects_[method_idx].emplace_back(std::move(aspect));
  return true;
} catch (...) {
  return false;
}

bool HttpRouteTable::AddGlobalAspect(
    OwnedPtr<HttpRequestAspectHandler> aspect) try {
  global_aspects_.emplace_back(std::move(aspect));
  return true;
} catch (...) {
  return false;
}

bool HttpRouteTable::SetWriteExpiry(HttpRequestMethod method,
                                    const std::string_view target,
                                    std::size_t expiry) {
  auto method_idx = static_cast<std::size_t>(method);
  if (method_idx >= kHttpRequestMethodNum ||
      !route_internal::IsValidParametricTarget(target)) {
    return false;
  }

  auto* route_layer = GetOrCreateRouteTableLayer(method, target);
  if (route_layer == nullptr) {
    return false;
  }

  route_layer->SetWriteExpiry(expiry);
  return true;
}

bool HttpRouteTable::SetReadExpiry(HttpRequestMethod method,
                                   const std::string_view target,
                                   std::size_t expiry) {
  auto method_idx = static_cast<std::size_t>(method);
  if (method_idx >= kHttpRequestMethodNum ||
      !route_internal::IsValidParametricTarget(target)) {
    return false;
  }

  auto* route_layer = GetOrCreateRouteTableLayer(method, target);
  if (route_layer == nullptr) {
    return false;
  }

  route_layer->SetReadExpiry(expiry);
  return true;
}

bool HttpRouteTable::SetMaxBodySize(HttpRequestMethod method,
                                    const std::string_view target,
                                    std::size_t max_body_size) {
  auto method_idx = static_cast<std::size_t>(method);
  if (method_idx >= kHttpRequestMethodNum ||
      !route_internal::IsValidParametricTarget(target)) {
    return false;
  }

  auto* route_layer = GetOrCreateRouteTableLayer(method, target);
  if (route_layer == nullptr) {
    return false;
  }

  route_layer->SetMaxBodySize(max_body_size);
  return true;
}

void HttpRouteTable::SetDefaultWriteExpiry(std::size_t expiry) noexcept {
  default_write_expiry_ = expiry;
}

void HttpRouteTable::SetDefaultReadExpiry(std::size_t expiry) noexcept {
  default_read_expiry_ = expiry;
}

void HttpRouteTable::SetDefaultMaxBodySize(std::size_t max_body_size) noexcept {
  default_max_body_size_ = max_body_size;
}

void HttpRouteTable::SetDefaultHandler(OwnedPtr<HttpRequestHandler> handler) {
  default_handler_ = std::move(handler);
}

HttpRouteTable::HttpRouteTable() noexcept
    : default_handler_(AllocateUnique<route_internal::EmptyRouteHandler>()) {
  for (auto& it : entrance_) {
    it = AllocateUnique<HttpRouteTableLayer>();
  }
}

void HttpRouteTable::CollectChildLayers(
    HttpRouteTableLayer& layer,
    std::vector<OwnedPtr<HttpRouteTableLayer>>& pending) {
  if (layer.default_route_ != nullptr) {
    pending.emplace_back(std::move(layer.default_route_));
  }

  pending.reserve(pending.size() + layer.map_.size());
  for (auto& [key, child] : layer.map_) {
    (void)key;
    if (child != nullptr) {
      pending.emplace_back(std::move(child));
    }
  }
  layer.map_.clear();
}

void HttpRouteTable::DestroyLayerTreeIterative(
    OwnedPtr<HttpRouteTableLayer>& root) noexcept {
  if (root == nullptr) {
    return;
  }

  // Deep route trees are user-controlled. Destroy iteratively so teardown
  // depth is bounded by heap storage instead of the call stack.
  std::vector<OwnedPtr<HttpRouteTableLayer>> pending;
  pending.emplace_back(std::move(root));
  while (!pending.empty()) {
    auto current = std::move(pending.back());
    pending.pop_back();
    if (current == nullptr) {
      continue;
    }
    CollectChildLayers(*current, pending);
  }
}

HttpRouteTable::~HttpRouteTable() noexcept {
  for (auto& root : entrance_) {
    DestroyLayerTreeIterative(root);
  }
}

}  // namespace bsrvcore
