/**
 * @file http_route_result.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-30
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#ifndef BSRVCORE_HTTP_ROUTE_RESULT_H_
#define BSRVCORE_HTTP_ROUTE_RESULT_H_

#include <cstddef>
#include <string>
#include <vector>

namespace bsrvcore {
class HttpRequestAspectHandler;
class HttpRequestHandler;

/**
 * @brief Result of routing an HTTP request
 *
 * Contains the matched handler, aspect chain, route parameters, and request
 * limits.
 */
struct HttpRouteResult {
  std::string current_location;         ///< Matched route path
  std::vector<std::string> parameters;  ///< Extracted route parameters
  std::vector<HttpRequestAspectHandler *>
      aspects;                  ///< Aspect handlers to execute
  HttpRequestHandler *handler;  ///< Main request handler
  std::size_t max_body_size;    ///< Maximum allowed request body size
  std::size_t read_expiry;      ///< Read operation timeout
  std::size_t write_expiry;     ///< Write operation timeout
};
}  // namespace bsrvcore

#endif
