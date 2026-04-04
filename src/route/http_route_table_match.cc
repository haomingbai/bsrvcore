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
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bsrvcore/internal/route/http_route_table.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/route/http_route_result.h"
#include "internal/http_route_table_detail.h"
#include "internal/http_route_table_layer.h"

using bsrvcore::HttpRequestAspectHandler;
using bsrvcore::HttpRequestMethod;
using bsrvcore::HttpRouteResult;
using bsrvcore::route_internal::HttpRouteTableLayer;

namespace bsrvcore {

HttpRouteResult HttpRouteTable::Route(HttpRequestMethod method,
                                      std::string_view target) noexcept {
  using boost::urls::parse_uri_reference;

  HttpRouteTableLayer* route_layer =
      entrance_[static_cast<std::size_t>(method)].get();
  if (route_layer == nullptr) {
    return BuildDefaultRouteResult(method);
  }

  auto parsed = parse_uri_reference(target);
  if (!parsed) {
    return BuildDefaultRouteResult(method);
  }

  std::string current_location;
  std::vector<std::string> parameter_values;
  if (!MatchSegments(*parsed, route_layer, current_location,
                     parameter_values)) {
    return BuildDefaultRouteResult(method);
  }

  auto aspects = CollectAspects(route_layer, method);
  auto* handler = route_layer->GetHandler();
  if (handler == nullptr) {
    return BuildDefaultRouteResult(method);
  }

  auto max_body_size = (route_layer->GetMaxBodySize() != 0u)
                           ? route_layer->GetMaxBodySize()
                           : default_max_body_size_;
  auto read_expiry = (route_layer->GetReadExpiry() != 0u)
                         ? route_layer->GetReadExpiry()
                         : default_read_expiry_;
  auto write_expiry = (route_layer->GetWriteExpiry() != 0u)
                          ? route_layer->GetWriteExpiry()
                          : default_write_expiry_;

  HttpRouteResult result;
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
  std::vector<HttpRequestAspectHandler*> aspects;
  aspects.reserve(global_aspects_.size());
  for (auto const& a : global_aspects_) {
    aspects.emplace_back(a.get());
  }

  // Routing failures still run the default handler inside the global aspect
  // envelope so cross-cutting behavior such as logging/auth can stay uniform.
  HttpRouteResult result;
  result.current_location = "/";
  result.route_template = "/";
  result.aspects = std::move(aspects);
  result.handler = default_handler_.get();
  result.max_body_size = default_max_body_size_;
  result.read_expiry = default_read_expiry_;
  result.write_expiry = default_write_expiry_;
  return result;
}

bool HttpRouteTable::MatchSegments(
    const boost::urls::url_view& url, HttpRouteTableLayer*& route_layer,
    std::string& out_location,
    std::vector<std::string>& out_parameter_values) const noexcept {
  out_location.clear();
  out_parameter_values.clear();

  // Walk the parsed URL directly so each segment stays tied to Boost.URL's
  // backing storage. Converting the proxy segment objects into detached
  // string_view values here would leave dangling references and make every
  // route miss after the split refactor.
  for (auto const& seg : url.segments()) {
    if (seg.empty()) {
      continue;
    }

    if (HttpRouteTableLayer* next = route_layer->GetRoute(std::string(seg))) {
      route_layer = next;
      out_location.push_back('/');
      out_location.append(seg);
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

    out_parameter_values.emplace_back(seg);
    route_layer = default_layer;
    out_location.push_back('/');
    out_location.append(seg);
  }

  if (out_location.empty()) {
    out_location = "/";
  }
  return true;
}

std::vector<std::string> HttpRouteTable::ExtractParamNames(
    std::string_view target) noexcept {
  std::vector<std::string> param_names;
  for (auto segment : route_internal::detail::SplitTargetSegments(target)) {
    const auto param_name = route_internal::detail::ExtractParamName(segment);
    if (!param_name.empty()) {
      param_names.emplace_back(param_name);
    }
  }
  return param_names;
}

std::string HttpRouteTable::NormalizeRouteTemplate(std::string_view target) {
  const std::string_view url = route_internal::detail::StripQuery(target);
  return url.empty() ? "/" : std::string(url);
}

std::unordered_map<std::string, std::string> HttpRouteTable::BuildParameterMap(
    const HttpRouteTableLayer& route_layer,
    std::vector<std::string> parameter_values) {
  std::unordered_map<std::string, std::string> parameters;
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

std::vector<HttpRequestAspectHandler*> HttpRouteTable::CollectAspects(
    HttpRouteTableLayer* route_layer, HttpRequestMethod method) const noexcept {
  std::vector<HttpRequestAspectHandler*> aspects;
  auto method_idx = static_cast<std::size_t>(method);

  std::size_t reserve_size = global_aspects_.size();
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

  // Route-local aspects are already stored in pre-order on the matched layer.
  // Appending them after global/method aspects yields the expected nesting:
  // global -> method -> route on entry, and the reverse on exit.
  auto route_aspects = route_layer->GetAspects();
  aspects.insert(aspects.end(), std::make_move_iterator(route_aspects.begin()),
                 std::make_move_iterator(route_aspects.end()));
  return aspects;
}

bool HttpRouteTable::HasTerminalConfiguration(
    const HttpRouteTableLayer& layer) noexcept {
  return layer.handler_ != nullptr || !layer.aspects_.empty() ||
         layer.max_body_size_ != 0 || layer.read_expiry_ != 0 ||
         layer.write_expiry_ != 0;
}

}  // namespace bsrvcore
