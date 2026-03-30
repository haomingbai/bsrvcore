/**
 * @file http_route_table.cc
 * @brief HttpRouteTable implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-28
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements route registration and request path matching.
 */

#include "bsrvcore/internal/route/http_route_table.h"

#include <algorithm>
#include <boost/url/parse.hpp>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bsrvcore/internal/route/empty_route_handler.h"
#include "bsrvcore/internal/route/http_route_table_layer.h"
#include "bsrvcore/route/cloneable_http_request_aspect_handler.h"
#include "bsrvcore/route/cloneable_http_request_handler.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"
#include "impl/http_route_target_validator.h"

using bsrvcore::HttpRequestAspectHandler;
using bsrvcore::HttpRequestHandler;
using bsrvcore::HttpRequestMethod;
using bsrvcore::HttpRouteResult;
using bsrvcore::HttpRouteTable;
using bsrvcore::route_internal::HttpRouteTableLayer;

namespace {

std::string_view StripQuery(std::string_view target) noexcept {
  return target.substr(0, target.find('?'));
}

bool IsParameterSegment(std::string_view segment) noexcept {
  return segment.size() >= 2 && segment.front() == '{' && segment.back() == '}';
}

std::string_view ExtractParamName(std::string_view segment) noexcept {
  if (!IsParameterSegment(segment)) {
    return {};
  }
  return segment.substr(1, segment.size() - 2);
}

}  // namespace

HttpRouteResult HttpRouteTable::Route(HttpRequestMethod method,
                                      std::string_view target) noexcept {
  using boost::urls::parse_uri_reference;

  // Initial layer.
  HttpRouteTableLayer* route_layer =
      entrance_[static_cast<size_t>(method)].get();

  // No route, return directly.
  if (route_layer == nullptr) {
    return BuildDefaultRouteResult(method);
  }

  // Resolve URI reference
  auto parsed = parse_uri_reference(target);
  if (!parsed) {
    return BuildDefaultRouteResult(method);
  }

  // Try to resolve the route and get current_location
  std::string current_location;
  std::vector<std::string> parameter_values;
  bool ok =
      MatchSegments(*parsed, route_layer, current_location, parameter_values);
  if (!ok) {
    return BuildDefaultRouteResult(method);
  }

  // Collect aspects（global + method-specific + route-specific）
  auto aspects = CollectAspects(route_layer, method);

  // Get the required handler
  HttpRequestHandler* handler = route_layer->GetHandler();

  if (!handler) {
    return BuildDefaultRouteResult(method);
  }

  auto max_body_size = default_max_body_size_;
  if (route_layer->GetMaxBodySize()) {
    max_body_size = route_layer->GetMaxBodySize();
  }

  auto read_expiry = default_read_expiry_;
  if (route_layer->GetReadExpiry()) {
    read_expiry = route_layer->GetReadExpiry();
  }

  auto write_expiry = default_write_expiry_;
  if (route_layer->GetWriteExpiry()) {
    write_expiry = route_layer->GetWriteExpiry();
  }

  HttpRouteResult result = {
      .current_location = std::move(current_location),
      .route_template = route_layer->GetRouteTemplate(),
      .parameters =
          BuildParameterMap(*route_layer, std::move(parameter_values)),
      .aspects = std::move(aspects),
      .handler = handler,
      .max_body_size = max_body_size,
      .read_expiry = read_expiry,
      .write_expiry = write_expiry,
  };

  return result;
}

bool HttpRouteTable::AddRouteEntry(HttpRequestMethod method,
                                   const std::string_view target,
                                   OwnedPtr<HttpRequestHandler> handler) {
  {
    using route_internal::IsValidParametricTarget;

    if (!IsValidParametricTarget(target)) {
      return false;
    }
  }

  auto method_idx = static_cast<size_t>(method);
  if (method_idx >= kHttpRequestMethodNum) {
    return false;
  }

  auto route_layer = GetOrCreateRouteTableLayer(method, target);
  if (!route_layer) {
    return false;
  }

  route_layer->SetHandler(std::move(handler));

  return true;
}

bool HttpRouteTable::AddExclusiveRouteEntry(
    HttpRequestMethod method, const std::string_view target,
    OwnedPtr<HttpRequestHandler> handler) {
  {
    using route_internal::IsValidParametricTarget;

    if (!IsValidParametricTarget(target)) {
      return false;
    }
  }

  auto method_idx = static_cast<size_t>(method);
  if (method_idx >= kHttpRequestMethodNum) {
    return false;
  }

  auto route_layer = GetOrCreateRouteTableLayer(method, target);
  if (!route_layer) {
    return false;
  }

  route_layer->SetHandler(std::move(handler));
  route_layer->SetIgnoreDefaultRoute(true);

  return true;
}

HttpRouteTableLayer* HttpRouteTable::GetOrCreateRouteTableLayer(
    HttpRequestMethod method, const std::string_view target) {
  auto route_layer = entrance_[static_cast<size_t>(method)].get();

  const std::string_view url = StripQuery(target);
  auto url_segments = url | std::views::split('/') |
                      std::ranges::views::transform([](auto&& word) {
                        return std::string_view(word.begin(), word.end());
                      });

  for (auto word : url_segments) {
    if (word.empty()) {
      continue;
    } else if (word.front() == '{') {
      // Parameter segments share one fallback edge at this tree level.
      if (HttpRouteTableLayer* current_layer = route_layer->GetDefaultRoute()) {
        route_layer = current_layer;
      } else {
        auto new_layer = AllocateUnique<HttpRouteTableLayer>();
        current_layer = new_layer.get();
        route_layer->SetDefaultRoute(std::move(new_layer));
        route_layer = current_layer;
      }
    } else {
      // Static segments keep exact-match children in the node map.
      if (auto current_layer = route_layer->GetRoute(std::string(word))) {
        route_layer = current_layer;
      } else {
        auto new_layer = AllocateUnique<HttpRouteTableLayer>();
        current_layer = new_layer.get();
        route_layer->SetRoute(std::string(word), std::move(new_layer));
        route_layer = current_layer;
      }
    }
  }

  route_layer->SetParamNames(ExtractParamNames(target));
  route_layer->SetRouteTemplate(NormalizeRouteTemplate(target));
  return route_layer;
}

HttpRouteResult HttpRouteTable::BuildDefaultRouteResult(
    [[maybe_unused]] HttpRequestMethod method) const noexcept {
  std::vector<HttpRequestAspectHandler*> aspects;
  aspects.reserve(global_aspects_.size());
  for (auto const& a : global_aspects_) {
    aspects.emplace_back(a.get());
  }

  HttpRouteResult result = {
      .current_location = "/",
      .route_template = "/",
      .parameters = {},
      .aspects = std::move(aspects),
      .handler = default_handler_.get(),
      .max_body_size = default_max_body_size_,
      .read_expiry = default_read_expiry_,
      .write_expiry = default_write_expiry_,
  };

  return result;
}

bool HttpRouteTable::MatchSegments(
    const boost::urls::url_view& url, HttpRouteTableLayer*& route_layer,
    std::string& out_location,
    std::vector<std::string>& out_parameter_values) const noexcept {
  out_location.clear();
  out_parameter_values.clear();

  for (auto const& seg : url.segments()) {
    if (seg.empty()) {
      continue;
    }

    // First try route with map.
    if (HttpRouteTableLayer* next = route_layer->GetRoute(std::string(seg))) {
      route_layer = next;
      out_location.push_back('/');
      out_location.append(seg);
    } else {
      // If the further route is prevented, then jump out and get the handler.
      if (route_layer->GetIgnoreDefaultRoute()) {
        break;
      }

      // Use parametric route.
      HttpRouteTableLayer* default_layer = route_layer->GetDefaultRoute();
      // If current parametre is not available.
      if (!default_layer) {
        return false;
      }

      out_parameter_values.emplace_back(seg);
      route_layer = default_layer;
      out_location.push_back('/');
      out_location.append(seg);
    }
  }

  if (out_location.empty()) {
    out_location = "/";
  }

  return true;
}

std::vector<std::string> HttpRouteTable::ExtractParamNames(
    std::string_view target) noexcept {
  std::vector<std::string> param_names;

  const std::string_view url = StripQuery(target);
  auto url_segments = url | std::views::split('/') |
                      std::ranges::views::transform([](auto&& word) {
                        return std::string_view(word.begin(), word.end());
                      });

  for (auto word : url_segments) {
    const std::string_view param_name = ExtractParamName(word);
    if (!param_name.empty()) {
      param_names.emplace_back(param_name);
    }
  }

  return param_names;
}

std::string HttpRouteTable::NormalizeRouteTemplate(std::string_view target) {
  const std::string_view url = StripQuery(target);
  if (url.empty()) {
    return "/";
  }

  return std::string(url);
}

std::unordered_map<std::string, std::string> HttpRouteTable::BuildParameterMap(
    const HttpRouteTableLayer& route_layer,
    std::vector<std::string> parameter_values) {
  std::unordered_map<std::string, std::string> parameters;
  const auto& param_names = route_layer.GetParamNames();
  const auto pair_count = std::min(param_names.size(), parameter_values.size());
  parameters.reserve(pair_count);

  for (std::size_t i = 0; i < pair_count; ++i) {
    parameters.emplace(param_names[i], std::move(parameter_values[i]));
  }

  return parameters;
}

bool HttpRouteTable::HasTerminalConfiguration(
    const HttpRouteTableLayer& layer) noexcept {
  return layer.handler_ != nullptr || !layer.aspects_.empty() ||
         layer.max_body_size_ != 0 || layer.read_expiry_ != 0 ||
         layer.write_expiry_ != 0;
}

std::string HttpRouteTable::JoinRouteTemplate(std::string_view prefix,
                                              std::string_view route_template) {
  std::string normalized_prefix = NormalizeRouteTemplate(prefix);
  std::string normalized_route = NormalizeRouteTemplate(route_template);

  if (normalized_prefix.size() > 1 && normalized_prefix.back() == '/') {
    normalized_prefix.pop_back();
  }

  if (normalized_prefix == "/") {
    return normalized_route;
  }

  if (normalized_route == "/") {
    return normalized_prefix;
  }

  return normalized_prefix + normalized_route;
}

void HttpRouteTable::PrefixRouteTemplates(HttpRouteTableLayer& layer,
                                          std::string_view prefix) {
  if (HasTerminalConfiguration(layer)) {
    layer.route_template_ = JoinRouteTemplate(prefix, layer.route_template_);
  }

  if (layer.default_route_ != nullptr) {
    PrefixRouteTemplates(*layer.default_route_, prefix);
  }

  for (auto& [key, child] : layer.map_) {
    (void)key;
    if (child != nullptr) {
      PrefixRouteTemplates(*child, prefix);
    }
  }
}

bool HttpRouteTable::CanMergeLayer(const HttpRouteTableLayer& dst,
                                   const HttpRouteTableLayer& src) noexcept {
  // Reject overlapping terminal configuration so mounts never overwrite an
  // existing handler/aspect/limit on the same logical route.
  if (HasTerminalConfiguration(src) && HasTerminalConfiguration(dst)) {
    return false;
  }

  if (src.default_route_ != nullptr && dst.default_route_ != nullptr &&
      !CanMergeLayer(*dst.default_route_, *src.default_route_)) {
    return false;
  }

  for (const auto& [key, src_child] : src.map_) {
    if (src_child == nullptr) {
      continue;
    }

    auto dst_it = dst.map_.find(key);
    if (dst_it == dst.map_.end() || dst_it->second == nullptr) {
      continue;
    }

    if (!CanMergeLayer(*dst_it->second, *src_child)) {
      return false;
    }
  }

  return true;
}

void HttpRouteTable::MoveMergeLayer(HttpRouteTableLayer& dst,
                                    HttpRouteTableLayer& src) {
  // Terminal state is copied once; child branches are merged recursively.
  if (src.handler_ != nullptr) {
    dst.handler_ = std::move(src.handler_);
    dst.param_names_ = std::move(src.param_names_);
    dst.route_template_ = std::move(src.route_template_);
  }

  if (!src.aspects_.empty()) {
    dst.aspects_ = std::move(src.aspects_);
  }

  if (src.max_body_size_ != 0) {
    dst.max_body_size_ = src.max_body_size_;
  }
  if (src.read_expiry_ != 0) {
    dst.read_expiry_ = src.read_expiry_;
  }
  if (src.write_expiry_ != 0) {
    dst.write_expiry_ = src.write_expiry_;
  }
  if (src.ignore_default_route_) {
    dst.ignore_default_route_ = true;
  }

  if (src.default_route_ != nullptr) {
    if (dst.default_route_ == nullptr) {
      dst.default_route_ = std::move(src.default_route_);
    } else {
      MoveMergeLayer(*dst.default_route_, *src.default_route_);
      src.default_route_.reset();
    }
  }

  for (auto& [key, src_child] : src.map_) {
    if (src_child == nullptr) {
      continue;
    }

    auto dst_it = dst.map_.find(key);
    if (dst_it == dst.map_.end() || dst_it->second == nullptr) {
      dst.map_.emplace(key, std::move(src_child));
      continue;
    }

    MoveMergeLayer(*dst_it->second, *src_child);
  }

  src.map_.clear();
}

bool HttpRouteTable::CloneLayer(const HttpRouteTableLayer& src,
                                OwnedPtr<HttpRouteTableLayer>& dst) {
  auto cloned = AllocateUnique<HttpRouteTableLayer>();
  cloned->max_body_size_ = src.max_body_size_;
  cloned->read_expiry_ = src.read_expiry_;
  cloned->write_expiry_ = src.write_expiry_;
  cloned->ignore_default_route_ = src.ignore_default_route_;
  cloned->param_names_ = src.param_names_;
  cloned->route_template_ = src.route_template_;

  if (src.handler_ != nullptr) {
    // Reusable blueprints only accept handlers that can deep-clone themselves.
    auto* cloneable_handler =
        dynamic_cast<const CloneableHttpRequestHandler*>(src.handler_.get());
    if (cloneable_handler == nullptr) {
      return false;
    }
    cloned->handler_ = cloneable_handler->Clone();
  }

  cloned->aspects_.reserve(src.aspects_.size());
  for (const auto& aspect : src.aspects_) {
    // Aspects follow the same rule as handlers: clone or reject the mount.
    auto* cloneable_aspect =
        dynamic_cast<const CloneableHttpRequestAspectHandler*>(aspect.get());
    if (cloneable_aspect == nullptr) {
      return false;
    }
    cloned->aspects_.emplace_back(cloneable_aspect->Clone());
  }

  if (src.default_route_ != nullptr) {
    if (!CloneLayer(*src.default_route_, cloned->default_route_)) {
      return false;
    }
  }

  for (const auto& [key, child] : src.map_) {
    if (child == nullptr) {
      continue;
    }

    OwnedPtr<HttpRouteTableLayer> cloned_child;
    if (!CloneLayer(*child, cloned_child)) {
      return false;
    }
    cloned->map_.emplace(key, std::move(cloned_child));
  }

  dst = std::move(cloned);
  return true;
}

std::vector<HttpRequestAspectHandler*> HttpRouteTable::CollectAspects(
    HttpRouteTableLayer* route_layer, HttpRequestMethod method) const noexcept {
  std::vector<HttpRequestAspectHandler*> aspects;
  auto method_idx = static_cast<size_t>(method);

  size_t reserve_size = global_aspects_.size();
  if (method_idx < global_specific_aspects_.size()) {
    reserve_size += global_specific_aspects_[method_idx].size();
  }
  reserve_size += route_layer->GetAspectNum();
  aspects.reserve(reserve_size);

  for (auto const& a : global_aspects_) {
    aspects.emplace_back(a.get());
  }

  if (method_idx < global_specific_aspects_.size()) {
    for (auto const& a : global_specific_aspects_[method_idx]) {
      aspects.emplace_back(a.get());
    }
  }

  auto route_aspects = route_layer->GetAspects();
  aspects.insert(aspects.end(), std::make_move_iterator(route_aspects.begin()),
                 std::make_move_iterator(route_aspects.end()));

  return aspects;
}

bool HttpRouteTable::AddAspect(HttpRequestMethod method,
                               const std::string_view target,
                               OwnedPtr<HttpRequestAspectHandler> aspect) {
  auto method_idx = static_cast<size_t>(method);
  if (method_idx >= kHttpRequestMethodNum) {
    return false;
  }

  if (!route_internal::IsValidParametricTarget(target)) {
    return false;
  }

  auto route_layer = GetOrCreateRouteTableLayer(method, target);
  if (!route_layer) {
    return false;
  }

  route_layer->AddAspect(std::move(aspect));
  return true;
}

bool HttpRouteTable::AddGlobalAspect(
    HttpRequestMethod method, OwnedPtr<HttpRequestAspectHandler> aspect) try {
  auto method_idx = static_cast<size_t>(method);
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
  auto method_idx = static_cast<size_t>(method);
  if (method_idx >= kHttpRequestMethodNum) {
    return false;
  }

  if (!route_internal::IsValidParametricTarget(target)) {
    return false;
  }

  auto route_layer = GetOrCreateRouteTableLayer(method, target);
  if (!route_layer) {
    return false;
  }

  route_layer->SetWriteExpiry(expiry);
  return true;
}

bool HttpRouteTable::SetReadExpiry(HttpRequestMethod method,
                                   const std::string_view target,
                                   std::size_t expiry) {
  auto method_idx = static_cast<size_t>(method);
  if (method_idx >= kHttpRequestMethodNum) {
    return false;
  }

  if (!route_internal::IsValidParametricTarget(target)) {
    return false;
  }

  auto route_layer = GetOrCreateRouteTableLayer(method, target);
  if (!route_layer) {
    return false;
  }

  route_layer->SetReadExpiry(expiry);
  return true;
}

bool HttpRouteTable::SetMaxBodySize(HttpRequestMethod method,
                                    const std::string_view target,
                                    std::size_t max_body_size) {
  auto method_idx = static_cast<size_t>(method);
  if (method_idx >= kHttpRequestMethodNum) {
    return false;
  }

  if (!route_internal::IsValidParametricTarget(target)) {
    return false;
  }

  auto route_layer = GetOrCreateRouteTableLayer(method, target);
  if (!route_layer) {
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
    : default_handler_(AllocateUnique<route_internal::EmptyRouteHandler>()),
      default_max_body_size_(16384),
      default_read_expiry_(4000),
      default_write_expiry_(4000) {
  for (auto& it : entrance_) {
    it = AllocateUnique<HttpRouteTableLayer>();
  }
}

HttpRouteTable::~HttpRouteTable() noexcept {
  for (auto& root : entrance_) {
    DestroyLayerTreeIterative(root);
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

bool HttpRouteTable::MountAt(std::string_view prefix, HttpRouteTable&& source) {
  if (!route_internal::IsValidParametricTarget(prefix)) {
    return false;
  }

  const auto get_or_create_mount_layer =
      [&](HttpRouteTableLayer* route_layer) -> HttpRouteTableLayer* {
    const std::string_view url = StripQuery(prefix);
    auto url_segments = url | std::views::split('/') |
                        std::ranges::views::transform([](auto&& word) {
                          return std::string_view(word.begin(), word.end());
                        });

    for (auto word : url_segments) {
      if (word.empty()) {
        continue;
      }

      if (IsParameterSegment(word)) {
        if (route_layer->default_route_ == nullptr) {
          route_layer->default_route_ = AllocateUnique<HttpRouteTableLayer>();
        }
        route_layer = route_layer->default_route_.get();
        continue;
      }

      auto it = route_layer->map_.find(std::string(word));
      if (it == route_layer->map_.end()) {
        auto new_layer = AllocateUnique<HttpRouteTableLayer>();
        auto* next = new_layer.get();
        route_layer->map_.emplace(std::string(word), std::move(new_layer));
        route_layer = next;
      } else {
        route_layer = it->second.get();
      }
    }

    return route_layer;
  };

  // First pass: normalize route templates and reject any conflicting nodes
  // before mutating the destination tree.
  for (std::size_t method_idx = 0; method_idx < entrance_.size();
       ++method_idx) {
    PrefixRouteTemplates(*source.entrance_[method_idx], prefix);

    HttpRouteTableLayer* mount_root =
        get_or_create_mount_layer(entrance_[method_idx].get());
    if (!CanMergeLayer(*mount_root, *source.entrance_[method_idx])) {
      return false;
    }
  }

  // Second pass: move the already-validated source subtrees into place.
  for (std::size_t method_idx = 0; method_idx < entrance_.size();
       ++method_idx) {
    HttpRouteTableLayer* mount_root =
        get_or_create_mount_layer(entrance_[method_idx].get());
    MoveMergeLayer(*mount_root, *source.entrance_[method_idx]);
  }

  return true;
}

bool HttpRouteTable::MountAt(std::string_view prefix,
                             const HttpRouteTable& source) {
  if (!route_internal::IsValidParametricTarget(prefix)) {
    return false;
  }

  HttpRouteTable cloned_source;
  for (std::size_t method_idx = 0; method_idx < entrance_.size();
       ++method_idx) {
    if (!CloneLayer(*source.entrance_[method_idx],
                    cloned_source.entrance_[method_idx])) {
      return false;
    }
  }

  return MountAt(prefix, std::move(cloned_source));
}
