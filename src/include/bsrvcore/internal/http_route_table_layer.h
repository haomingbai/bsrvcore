/**
 * @file http_route_table_layer.h
 * @brief Internal routing layer for hierarchical HTTP route matching
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-25
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Represents a single layer in the hierarchical HTTP routing tree.
 * Each layer corresponds to a path segment and manages sub-routes,
 * handlers, aspects, and request limits for that segment.
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_HTTP_ROUTE_TABLE_LAYER_H_
#define BSRVCORE_INTERNAL_HTTP_ROUTE_TABLE_LAYER_H_

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "bsrvcore/trait.h"

namespace bsrvcore {

class HttpRequestHandler;
class HttpRequestAspectHandler;

namespace route_internal {

/**
 * @brief Single layer in hierarchical HTTP routing tree
 *
 * Each layer represents a path segment in the URL and forms a tree structure
 * for efficient route matching. Supports sub-routes, parameter routes,
 * aspect handlers, and configurable request limits.
 *
 * @code
 * // Example routing tree structure:
 * // Layer: "" (root: get handler)
 * //   ├── "api" -> Layer
 * //   │      ├── "users" -> Layer (with certain route direction)
 * //   │      └── default_route -> Layer (for parameter routes like {id})
 * //   └── "static" -> Layer (with exclusive handler)
 * @endcode
 */
class HttpRouteTableLayer : NonCopyableNonMovable<HttpRouteTableLayer> {
 public:
  /**
   * @brief Set maximum request body size for this route layer
   * @param max_body_size Maximum body size in bytes
   */
  void SetMaxBodySize(std::size_t max_body_size) noexcept;

  /**
   * @brief Get maximum request body size for this route layer
   * @return Maximum body size in bytes
   */
  std::size_t GetMaxBodySize() const noexcept;

  /**
   * @brief Set read timeout for this route layer
   * @param expiry Read timeout in milliseconds
   */
  void SetReadExpiry(std::size_t expiry) noexcept;

  /**
   * @brief Get read timeout for this route layer
   * @return Read timeout in milliseconds
   */
  std::size_t GetReadExpiry() const noexcept;

  /**
   * @brief Set write timeout for this route layer
   * @param expiry Write timeout in milliseconds
   */
  void SetWriteExpiry(std::size_t expiry) noexcept;

  /**
   * @brief Get write timeout for this route layer
   * @return Write timeout in milliseconds
   */
  std::size_t GetWriteExpiry() const noexcept;

  /**
   * @brief Set the request handler for this route layer
   * @param handler HTTP request handler
   * @return true if handler was set successfully
   */
  bool SetHandler(std::unique_ptr<HttpRequestHandler> handler) noexcept;

  /**
   * @brief Set the default sub-route for parameter matching
   * @param route Default route layer for unmatched segments
   * @return true if default route was set successfully
   *
   * @note The default route is used for parameter captures like {id}
   */
  bool SetDefaultRoute(std::unique_ptr<HttpRouteTableLayer> route) noexcept;

  /**
   * @brief Add a sub-route for a specific path segment
   * @param key Path segment (e.g., "users", "api")
   * @param link Sub-route layer for this segment
   * @return true if sub-route was added successfully
   */
  bool SetRoute(std::string key, std::unique_ptr<HttpRouteTableLayer> link);

  /**
   * @brief Enable/disable default route matching for exclusive routes
   * @param flag true to ignore default route, false to allow it
   *
   * @note When enabled, parameter routes at this level are bypassed
   *       in favor of exact matches (used for exclusive routes)
   */
  void SetIgnoreDefaultRoute(bool flag) noexcept;

  /**
   * @brief Get the default sub-route for parameter matching
   * @return Pointer to default route layer, nullptr if not set
   */
  HttpRouteTableLayer *GetDefaultRoute() noexcept;

  /**
   * @brief Get sub-route for a specific path segment (copy version)
   * @param key Path segment to look up
   * @return Pointer to sub-route layer, nullptr if not found
   */
  HttpRouteTableLayer *GetRoute(const std::string &key) noexcept;

  /**
   * @brief Get sub-route for a specific path segment (move version)
   * @param key Path segment to look up (will be moved)
   * @return Pointer to sub-route layer, nullptr if not found
   */
  HttpRouteTableLayer *GetRoute(std::string &&key) noexcept;

  /**
   * @brief Get the request handler for this route layer
   * @return Pointer to request handler, nullptr if not set
   */
  HttpRequestHandler *GetHandler() noexcept;

  /**
   * @brief Add an aspect handler to this route layer
   * @param aspect Aspect handler to add
   * @return true if aspect was added successfully
   */
  bool AddAspect(std::unique_ptr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Get the number of aspect handlers attached to this layer
   * @return Number of aspect handlers
   */
  std::size_t GetAspectNum() const noexcept;

  /**
   * @brief Get all aspect handlers attached to this layer
   * @return Vector of aspect handler pointers
   */
  std::vector<HttpRequestAspectHandler *> GetAspects() const;

  /**
   * @brief Check if default route matching is disabled
   * @return true if default route is ignored
   */
  bool GetIgnoreDefaultRoute() noexcept;

  /**
   * @brief Construct a new routing layer
   */
  HttpRouteTableLayer();

 private:
  std::unordered_map<std::string, std::unique_ptr<HttpRouteTableLayer>>
      map_;  ///< Sub-routes by path segment
  std::vector<std::unique_ptr<HttpRequestAspectHandler>>
      aspects_;  ///< Aspect handlers for this layer
  std::unique_ptr<HttpRouteTableLayer>
      default_route_;  ///< Default route for parameter matching
  std::unique_ptr<HttpRequestHandler>
      handler_;                ///< Request handler for this layer
  std::size_t max_body_size_;  ///< Maximum request body size
  std::size_t read_expiry_;    ///< Read operation timeout
  std::size_t write_expiry_;   ///< Write operation timeout
  bool ignore_default_route_;  ///< Flag to bypass parameter routes
};

}  // namespace route_internal

}  // namespace bsrvcore

#endif
