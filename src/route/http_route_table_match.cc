/**
 * @file http_route_table_match.cc
 * @brief Request matching logic for HttpRouteTable.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <boost/url/parse.hpp>
#include <boost/url/url_view.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/internal/route/http_route_table.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/route/http_route_result.h"
#include "internal/http_route_table_detail.h"
#include "internal/http_route_table_layer.h"

using bsrvcore::HttpRequestAspectHandler;
using bsrvcore::HttpRequestMethod;
using bsrvcore::HttpRouteResult;
using bsrvcore::route_internal::HttpRouteResultInternal;
using bsrvcore::route_internal::HttpRouteTableLayer;

namespace bsrvcore {

HttpRouteResult HttpRouteTable::Route(HttpRequestMethod method,
                                      std::string_view target) noexcept {
  return route_internal::ToPublicRouteResult(RouteInternal(method, target));
}

HttpRouteResultInternal HttpRouteTable::RouteInternal(
    HttpRequestMethod method, std::string_view target) noexcept {
  using boost::urls::parse_uri_reference;

  HttpRouteTableLayer* route_layer =
      entrance_[static_cast<std::size_t>(method)].get();
  if (route_layer == nullptr) {
    return BuildDefaultRouteResultInternal(method);
  }

  auto parsed = parse_uri_reference(target);
  if (!parsed) {
    return BuildDefaultRouteResultInternal(method);
  }

  AllocatedString current_location;
  AllocatedVector<AllocatedString> parameter_values;
  AllocatedVector<HttpRouteTableLayer*> matched_layers;
  if (!MatchSegments(*parsed, route_layer, current_location, parameter_values,
                     matched_layers)) {
    return BuildDefaultRouteResultInternal(method);
  }

  auto* handler = route_layer->GetHandler();
  if (handler == nullptr) {
    return BuildDefaultRouteResultInternal(method);
  }
  auto aspects = CollectAspects(matched_layers, route_layer, method);

  auto max_body_size = (route_layer->GetMaxBodySize() != 0u)
                           ? route_layer->GetMaxBodySize()
                           : default_max_body_size_;
  auto read_expiry = (route_layer->GetReadExpiry() != 0u)
                         ? route_layer->GetReadExpiry()
                         : default_read_expiry_;
  auto write_expiry = (route_layer->GetWriteExpiry() != 0u)
                          ? route_layer->GetWriteExpiry()
                          : default_write_expiry_;

  HttpRouteResultInternal result;
  result.current_location = std::move(current_location);
  result.route_template = route_layer->GetRouteTemplate();
  result.parameters =
      BuildParameterMap(*route_layer, std::move(parameter_values));
  result.aspects = std::move(aspects);
  result.handler = handler;
  result.max_body_size = max_body_size;
  result.read_expiry = read_expiry;
  result.write_expiry = write_expiry;
  return result;
}

HttpRouteResult HttpRouteTable::BuildDefaultRouteResult(
    [[maybe_unused]] HttpRequestMethod method) const noexcept {
  return route_internal::ToPublicRouteResult(
      BuildDefaultRouteResultInternal(method));
}

HttpRouteResultInternal HttpRouteTable::BuildDefaultRouteResultInternal(
    [[maybe_unused]] HttpRequestMethod method) const noexcept {
  AllocatedVector<HttpRequestAspectHandler*> aspects;
  aspects.reserve(global_aspects_.size());
  std::ranges::for_each(global_aspects_, [&aspects](const auto& aspect) {
    aspects.emplace_back(aspect.get());
  });

  // Routing failures still run the default handler inside the global aspect
  // envelope so cross-cutting behavior such as logging/auth can stay uniform.
  HttpRouteResultInternal result;
  result.current_location = detail::ToAllocatedString("/");
  result.route_template = detail::ToAllocatedString("/");
  result.aspects = std::move(aspects);
  result.handler = default_handler_.get();
  result.max_body_size = default_max_body_size_;
  result.read_expiry = default_read_expiry_;
  result.write_expiry = default_write_expiry_;
  return result;
}

bool HttpRouteTable::MatchSegments(
    const boost::urls::url_view& url, HttpRouteTableLayer*& route_layer,
    AllocatedString& out_location,
    AllocatedVector<AllocatedString>& out_parameter_values,
    AllocatedVector<HttpRouteTableLayer*>& out_matched_layers) const noexcept {
  out_location.clear();
  out_parameter_values.clear();
  out_matched_layers.clear();

  if (route_layer != nullptr) {
    out_matched_layers.emplace_back(route_layer);
  }

  // Walk the parsed URL directly so each segment stays tied to Boost.URL's
  // backing storage. Converting the proxy segment objects into detached
  // string_view values here would leave dangling references and make every
  // route miss after the split refactor.
  for (auto const& seg : url.segments()) {
    if (seg.empty()) {
      continue;
    }

    const std::string segment_text(seg);

    if (HttpRouteTableLayer* next = route_layer->GetRoute(segment_text)) {
      route_layer = next;
      out_location.push_back('/');
      out_location.append(segment_text);
      out_matched_layers.emplace_back(route_layer);
      continue;
    }

    if (route_layer->GetIgnoreDefaultRoute()) {
      // Exclusive routes intentionally stop parametric fallback at this node.
      // Matching then ends at the exact layer that declared the exclusivity.
      break;
    }

    HttpRouteTableLayer* default_layer = route_layer->GetDefaultRoute();
    if (default_layer == nullptr) {
      return false;
    }

    out_parameter_values.emplace_back(detail::ToAllocatedString(segment_text));
    route_layer = default_layer;
    out_location.push_back('/');
    out_location.append(segment_text);
    out_matched_layers.emplace_back(route_layer);
  }

  if (out_location.empty()) {
    out_location = "/";
  }
  return true;
}

AllocatedVector<AllocatedString> HttpRouteTable::ExtractParamNames(
    std::string_view target) noexcept {
  AllocatedVector<AllocatedString> param_names;
  for (auto segment : route_internal::detail::SplitTargetSegments(target)) {
    const auto param_name = route_internal::detail::ExtractParamName(segment);
    if (!param_name.empty()) {
      param_names.emplace_back(detail::ToAllocatedString(param_name));
    }
  }
  return param_names;
}

AllocatedString HttpRouteTable::NormalizeRouteTemplate(
    std::string_view target) {
  const std::string_view url = route_internal::detail::StripQuery(target);
  return url.empty() ? detail::ToAllocatedString("/")
                     : detail::ToAllocatedString(url);
}

AllocatedStringMap HttpRouteTable::BuildParameterMap(
    const HttpRouteTableLayer& route_layer,
    AllocatedVector<AllocatedString> parameter_values) {
  AllocatedStringMap parameters;
  const auto& param_names = route_layer.GetParamNames();
  const auto pair_count = std::min(param_names.size(), parameter_values.size());
  parameters.reserve(pair_count);

  // Parameter names live on the terminal layer, while values are captured on
  // the walk down the tree. Zip them only at the end so matching logic stays
  // independent of how many static segments were traversed in between.
  for (std::size_t i = 0; i < pair_count; ++i) {
    parameters.emplace(param_names[i], std::move(parameter_values[i]));
  }
  return parameters;
}

AllocatedVector<HttpRequestAspectHandler*> HttpRouteTable::CollectAspects(
    const AllocatedVector<HttpRouteTableLayer*>& matched_layers,
    HttpRouteTableLayer* route_layer, HttpRequestMethod method) const noexcept {
  AllocatedVector<HttpRequestAspectHandler*> aspects;
  auto method_idx = static_cast<std::size_t>(method);

  std::size_t reserve_size = global_aspects_.size();
  if (method_idx < global_specific_aspects_.size()) {
    reserve_size += global_specific_aspects_[method_idx].size();
  }
  for (const auto* layer : matched_layers) {
    if (layer != nullptr) {
      reserve_size += layer->aspects_.size();
    }
  }
  reserve_size += route_layer->terminal_aspects_.size();
  aspects.reserve(reserve_size);

  for (auto const& a : global_aspects_) {
    aspects.emplace_back(a.get());
  }
  if (method_idx < global_specific_aspects_.size()) {
    for (auto const& a : global_specific_aspects_[method_idx]) {
      aspects.emplace_back(a.get());
    }
  }

  // Subtree aspects follow the successfully matched layer stack from root to
  // leaf, then terminal aspects on the final layer wrap the concrete handler.
  for (const auto* layer : matched_layers) {
    if (layer == nullptr) {
      continue;
    }
    for (const auto& subtree_aspect : layer->aspects_) {
      aspects.emplace_back(subtree_aspect.get());
    }
  }

  for (const auto& terminal_aspect : route_layer->terminal_aspects_) {
    aspects.emplace_back(terminal_aspect.get());
  }
  return aspects;
}

bool HttpRouteTable::HasTerminalConfiguration(
    const HttpRouteTableLayer& layer) noexcept {
  return layer.handler_ != nullptr || !layer.terminal_aspects_.empty() ||
         layer.max_body_size_ != 0 || layer.read_expiry_ != 0 ||
         layer.write_expiry_ != 0;
}

}  // namespace bsrvcore
