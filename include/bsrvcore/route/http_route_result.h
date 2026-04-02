/**
 * @file http_route_result.h
 * @brief Routing result container returned by the router.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-30
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines HttpRouteResult, a lightweight struct that carries the matched route
 * handler, aspect chain, extracted path parameters, and per-route limits.
 */

#pragma once

#ifndef BSRVCORE_ROUTE_HTTP_ROUTE_RESULT_H_
#define BSRVCORE_ROUTE_HTTP_ROUTE_RESULT_H_

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace bsrvcore {
class HttpRequestAspectHandler;
class HttpRequestHandler;

/**
 * @brief Result of routing an HTTP request
 *
 * Contains the matched handler, aspect chain, named route parameters, and
 * request limits.
 */
struct HttpRouteResult {
  std::string current_location;  ///< Matched concrete request path
  std::string route_template;    ///< Matched route template path
  std::unordered_map<std::string, std::string>
      parameters;  ///< Extracted route parameters by name
  std::vector<HttpRequestAspectHandler*>
      aspects;                    ///< Aspect handlers to execute
  HttpRequestHandler* handler{};  ///< Main request handler
  std::size_t max_body_size{};    ///< Maximum allowed request body size
  std::size_t read_expiry{};      ///< Read operation timeout
  std::size_t write_expiry{};     ///< Write operation timeout
};
}  // namespace bsrvcore

#endif
