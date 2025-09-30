/**
 * @file http_route_table.h
 * @brief HTTP routing table with aspect-oriented programming support
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-24
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Thread-safe HTTP routing system supporting route parameters, aspect-oriented
 * programming, exclusive routes, and configurable request limits.
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_HTTP_ROUTE_TABLE_H_
#define BSRVCORE_INTERNAL_HTTP_ROUTE_TABLE_H_

#include <array>
#include <boost/url/url_view.hpp>
#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_route_result.h"
#include "bsrvcore/internal/http_route_table_layer.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

class HttpRequestHandler;

class HttpRequestAspectHandler;

/**
 * @brief Thread-safe HTTP routing table with AOP support
 *
 * Manages HTTP route registration and matching with support for:
 * - Route parameters (e.g., /users/{id})
 * - Aspect-oriented programming (pre/post request processing)
 * - Exclusive routes (parameter routes bypass)
 * - Configurable request limits per route
 *
 * @code
 * // Example usage
 * auto route_table = std::make_unique<HttpRouteTable>();
 *
 * // Add a regular route
 * route_table->AddRouteEntry(HttpRequestMethod::GET, "/users/{id}",
 *                           std::make_unique<UserHandler>());
 *
 * // Add an exclusive route (static file serving)
 * route_table->AddExclusiveRouteEntry(HttpRequestMethod::GET, "/static",
 *                                    std::make_unique<StaticFileHandler>());
 *
 * // Add a global aspect (authentication)
 * route_table->AddGlobalAspect(std::make_unique<AuthAspect>());
 *
 * // Route a request
 * auto result = route_table->Route(HttpRequestMethod::GET, "/users/123");
 * @endcode
 */
class HttpRouteTable : NonCopyableNonMovable<HttpRouteTable> {
 public:
  /**
   * @brief Route an HTTP request to the appropriate handler
   * @param method HTTP request method
   * @param path Request path to match
   * @return Routing result with handler, aspects, and parameters
   */
  HttpRouteResult Route(HttpRequestMethod method,
                        const std::string_view path) noexcept;

  /**
   * @brief Add a route entry with a handler
   * @param method HTTP method this route applies to
   * @param target Route pattern (supports parameters like {name})
   * @param handler Request handler for this route
   * @return true if route was added successfully
   */
  bool AddRouteEntry(HttpRequestMethod method, const std::string_view target,
                     std::unique_ptr<HttpRequestHandler> handler);

  /**
   * @brief Add an aspect handler to a specific route
   * @param method HTTP method this aspect applies to
   * @param target Route pattern to attach aspect to
   * @param aspect Aspect handler to add
   * @return true if aspect was added successfully
   */
  bool AddAspect(HttpRequestMethod method, const std::string_view target,
                 std::unique_ptr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Add a global aspect for a specific HTTP method
   * @param method HTTP method this aspect applies to
   * @param aspect Aspect handler to add
   * @return true if aspect was added successfully
   */
  bool AddGlobalAspect(HttpRequestMethod method,
                       std::unique_ptr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Add a global aspect for all HTTP methods
   * @param aspect Aspect handler to add
   * @return true if aspect was added successfully
   */
  bool AddGlobalAspect(std::unique_ptr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Add an exclusive route that bypasses parameter routes
   *
   * Exclusive routes take precedence over parameter routes at the same base
   * path. This is useful for static file serving where exact matches should
   * bypass parameter routing.
   *
   * @param method HTTP method this route applies to
   * @param target Route pattern
   * @param handler Request handler for this route
   * @return true if route was added successfully
   *
   * @code
   * // With these routes:
   * AddExclusiveRouteEntry(GET, "/static", staticHandler);
   * AddRouteEntry(GET, "/static/{file}", paramHandler);
   *
   * // Requests will route as:
   * // "/static" -> staticHandler (exact match)
   * // "/static/abc" -> staticHandler (exclusive bypasses parameter route)
   * // "/static/def" -> staticHandler (exclusive bypasses parameter route)
   * @endcode
   */
  bool AddExclusiveRouteEntry(HttpRequestMethod method,
                              const std::string_view target,
                              std::unique_ptr<HttpRequestHandler> handler);

  /**
   * @brief Set read timeout for a specific route
   * @param method HTTP method
   * @param target Route pattern
   * @param expiry Read timeout in milliseconds
   * @return true if timeout was set successfully
   */
  bool SetReadExpiry(HttpRequestMethod method, const std::string_view target,
                     std::size_t expiry);

  /**
   * @brief Set write timeout for a specific route
   * @param method HTTP method
   * @param target Route pattern
   * @param expiry Write timeout in milliseconds
   * @return true if timeout was set successfully
   */
  bool SetWriteExpiry(HttpRequestMethod method, const std::string_view target,
                      std::size_t expiry);

  /**
   * @brief Set maximum body size for a specific route
   * @param method HTTP method
   * @param target Route pattern
   * @param max_body_size Maximum body size in bytes
   * @return true if limit was set successfully
   */
  bool SetMaxBodySize(HttpRequestMethod method, const std::string_view target,
                      std::size_t max_body_size);

  /**
   * @brief Set default read timeout for all routes
   * @param expiry Read timeout in milliseconds
   */
  void SetDefaultReadExpiry(std::size_t expiry) noexcept;

  /**
   * @brief Set default write timeout for all routes
   * @param expiry Write timeout in milliseconds
   */
  void SetDefaultWriteExpiry(std::size_t expiry) noexcept;

  /**
   * @brief Set default maximum body size for all routes
   * @param max_body_size Maximum body size in bytes
   */
  void SetDefaultMaxBodySize(std::size_t max_body_size) noexcept;

  /**
   * @brief Construct an empty routing table
   */
  HttpRouteTable() noexcept;

 private:
  /**
   * @brief A helper function to get the correct route layer. When the layer is
   * not available, create one.
   * @param target The url target to find
   * @return The route table layer found.
   */
  route_internal::HttpRouteTableLayer *GetOrCreateRouteTableLayer(
      HttpRequestMethod method, const std::string_view target);

  /**
   * @brief Build a default route result used when routing fails or no layer
   * exists.
   * @param method The HTTP method for which the default result is built.
   * @return A HttpRouteResult configured with the root location, default
   * handler, and global aspects.
   * @note This function does not throw and is noexcept.
   */
  HttpRouteResult BuildDefaultRouteResult(
      HttpRequestMethod method) const noexcept;

  /**
   * @brief Match URL segments against the route tree and extract parameters.
   * @param url The parsed URL view whose segments will be matched.
   * @param route_layer In/out pointer to the current route layer; updated to
   * the matched layer on success.
   * @param out_location Output string that receives the matched route template
   * (e.g. "/users/{param0}/info").
   * @param out_parameters Output vector receiving parameter values extracted
   * from the URL.
   * @return True if matching succeeded and route_layer was advanced to a valid
   * layer; false otherwise.
   * @note This function is noexcept and will not throw.
   */
  bool MatchSegments(const boost::urls::url_view &url,
                     route_internal::HttpRouteTableLayer *&route_layer,
                     std::string &out_location,
                     std::vector<std::string> &out_parameters) const noexcept;

  /**
   * @brief Collect aspect handlers in order: global, method-specific, then
   * route-specific.
   * @param route_layer The route layer providing route-specific aspects.
   * @param method The HTTP method whose method-specific aspects will be
   * included.
   * @return A vector of non-owning pointers to HttpRequestAspectHandler in
   * execution order.
   * @note Returned pointers are non-owning; lifetime is bound to the stored
   * handlers.
   */
  std::vector<HttpRequestAspectHandler *> CollectAspects(
      route_internal::HttpRouteTableLayer *route_layer,
      HttpRequestMethod method) const noexcept;

  static constexpr size_t kHttpRequestMethodNum = 9;
  std::shared_mutex mtx_;  ///< Read-write lock for thread safety
  std::array<std::unique_ptr<route_internal::HttpRouteTableLayer>,
             kHttpRequestMethodNum>
      entrance_;  ///< Routing layers per HTTP method
  std::array<std::vector<std::unique_ptr<HttpRequestAspectHandler>>,
             kHttpRequestMethodNum>
      global_specific_aspects_;  ///< Method-specific global aspects
  std::vector<std::unique_ptr<HttpRequestAspectHandler>>
      global_aspects_;  ///< Global aspects for all methods
  std::unique_ptr<HttpRequestHandler>
      default_handler_;  ///< Fallback handler for unmatched routes
  std::size_t default_max_body_size_;  ///< Default maximum request body size
  std::size_t default_read_expiry_;    ///< Default read timeout
  std::size_t default_write_expiry_;   ///< Default write timeout
};

}  // namespace bsrvcore

#endif
