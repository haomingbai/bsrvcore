/**
 * @file http_route_result_internal.h
 * @brief Internal allocator-backed routing result for hot-path usage.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-25
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_ROUTE_HTTP_ROUTE_RESULT_INTERNAL_H_
#define BSRVCORE_INTERNAL_ROUTE_HTTP_ROUTE_RESULT_INTERNAL_H_

#include <cstddef>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/route/http_route_result.h"

namespace bsrvcore {

class HttpRequestAspectHandler;
class HttpRequestHandler;

namespace route_internal {

/**
 * @brief Allocator-backed routing result used inside routing and connections.
 */
struct HttpRouteResultInternal
    : public CopyableMovable<HttpRouteResultInternal> {
  AllocatedString current_location;
  AllocatedString route_template;
  AllocatedStringMap parameters;
  AllocatedVector<HttpRequestAspectHandler*> aspects;
  HttpRequestHandler* handler{};
  std::size_t max_body_size{};
  std::size_t read_expiry{};
  std::size_t write_expiry{};
};

/**
 * @brief Convert internal allocator-backed route result to public result.
 */
inline HttpRouteResult ToPublicRouteResult(
    const HttpRouteResultInternal& internal) {
  HttpRouteResult public_result;
  public_result.current_location =
      detail::ToStdString(internal.current_location);
  public_result.route_template = detail::ToStdString(internal.route_template);
  public_result.parameters.reserve(internal.parameters.size());
  for (const auto& [key, value] : internal.parameters) {
    public_result.parameters.emplace(detail::ToStdString(key),
                                     detail::ToStdString(value));
  }
  public_result.aspects = detail::ToStdVector(internal.aspects);
  public_result.handler = internal.handler;
  public_result.max_body_size = internal.max_body_size;
  public_result.read_expiry = internal.read_expiry;
  public_result.write_expiry = internal.write_expiry;
  return public_result;
}

/**
 * @brief Move-convert internal allocator-backed route result to public result.
 */
inline HttpRouteResult ToPublicRouteResult(HttpRouteResultInternal&& internal) {
  HttpRouteResult public_result;
  public_result.current_location =
      detail::ToStdString(internal.current_location);
  public_result.route_template = detail::ToStdString(internal.route_template);
  public_result.parameters.reserve(internal.parameters.size());
  for (auto& [key, value] : internal.parameters) {
    public_result.parameters.emplace(detail::ToStdString(key),
                                     detail::ToStdString(value));
  }
  public_result.aspects = detail::ToStdVector(std::move(internal.aspects));
  public_result.handler = internal.handler;
  public_result.max_body_size = internal.max_body_size;
  public_result.read_expiry = internal.read_expiry;
  public_result.write_expiry = internal.write_expiry;
  return public_result;
}

/**
 * @brief Convert public route result to internal allocator-backed result.
 */
inline HttpRouteResultInternal ToInternalRouteResult(
    const HttpRouteResult& public_result) {
  HttpRouteResultInternal internal;
  internal.current_location =
      detail::ToAllocatedString(public_result.current_location);
  internal.route_template =
      detail::ToAllocatedString(public_result.route_template);
  internal.parameters.reserve(public_result.parameters.size());
  for (const auto& [key, value] : public_result.parameters) {
    internal.parameters.emplace(detail::ToAllocatedString(key),
                                detail::ToAllocatedString(value));
  }
  internal.aspects = detail::ToAllocatedVector(public_result.aspects);
  internal.handler = public_result.handler;
  internal.max_body_size = public_result.max_body_size;
  internal.read_expiry = public_result.read_expiry;
  internal.write_expiry = public_result.write_expiry;
  return internal;
}

/**
 * @brief Move-convert public route result to allocator-backed internal result.
 */
inline HttpRouteResultInternal ToInternalRouteResult(
    HttpRouteResult&& public_result) {
  HttpRouteResultInternal internal;
  internal.current_location =
      detail::ToAllocatedString(public_result.current_location);
  internal.route_template =
      detail::ToAllocatedString(public_result.route_template);
  internal.parameters.reserve(public_result.parameters.size());
  for (auto& [key, value] : public_result.parameters) {
    internal.parameters.emplace(detail::ToAllocatedString(key),
                                detail::ToAllocatedString(value));
  }
  internal.aspects = detail::ToAllocatedVector(std::move(public_result.aspects));
  internal.handler = public_result.handler;
  internal.max_body_size = public_result.max_body_size;
  internal.read_expiry = public_result.read_expiry;
  internal.write_expiry = public_result.write_expiry;
  return internal;
}

}  // namespace route_internal

}  // namespace bsrvcore

#endif  // BSRVCORE_INTERNAL_ROUTE_HTTP_ROUTE_RESULT_INTERNAL_H_
