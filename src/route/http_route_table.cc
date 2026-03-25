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

#include "bsrvcore/internal/http_route_table.h"

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

#include "bsrvcore/http_request_aspect_handler.h"
#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/internal/empty_route_handler.h"
#include "bsrvcore/internal/http_route_table_layer.h"
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
  return segment.size() >= 2 && segment.front() == '{' &&
         segment.back() == '}';
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
  bool ok = MatchSegments(*parsed, route_layer, current_location,
                          parameter_values);
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
      .parameters = BuildParameterMap(*route_layer, std::move(parameter_values)),
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
