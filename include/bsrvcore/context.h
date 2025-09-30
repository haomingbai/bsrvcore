/**
 * @file context.h
 * @brief Thread-safe context container for request-scoped attributes
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-25
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides a thread-safe key-value store for request-scoped attributes.
 * Supports polymorphic attributes with shared ownership and concurrent access.
 */

#pragma once

#ifndef BSRVCORE_CONTEXT_H_
#define BSRVCORE_CONTEXT_H_

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "bsrvcore/trait.h"

namespace bsrvcore {
class Attribute;

/**
 * @brief Thread-safe container for request-scoped attributes
 *
 * Context provides a thread-safe way to store and retrieve polymorphic
 * attributes during request processing. It's commonly used to pass
 * request-specific data between different stages of HTTP processing,
 * middleware, and handlers.
 *
 * @code
 * // Example usage in request handler
 * auto context = std::make_shared<Context>();
 *
 * // Store user authentication info
 * auto user_attr = std::make_shared<UserAttribute>();
 * context->SetAttribute("user", user_attr);
 *
 * // Retrieve in another handler
 * if (context->HasAttribute("user")) {
 *     auto user = context->GetAttribute("user");
 *     // Process user data
 * }
 *
 * // Thread-safe for concurrent access
 * std::thread t1([&]() {
 *     context->SetAttribute("data1", std::make_shared<DataAttribute>());
 * });
 * std::thread t2([&]() {
 *     auto data = context->GetAttribute("data2");
 * });
 * @endcode
 */
class Context : NonCopyableNonMovable<Context> {
 public:
  /**
   * @brief Retrieve an attribute by key (copy version)
   * @param key Attribute key to look up
   * @return Shared pointer to attribute, nullptr if not found
   */
  std::shared_ptr<Attribute> GetAttribute(const std::string &key) noexcept;

  /**
   * @brief Retrieve an attribute by key (move version)
   * @param key Attribute key to look up (will be moved)
   * @return Shared pointer to attribute, nullptr if not found
   */
  std::shared_ptr<Attribute> GetAttribute(std::string &&key) noexcept;

  /**
   * @brief Store an attribute with the specified key
   * @param key Attribute key (will be moved)
   * @param val Attribute value (shared ownership)
   *
   * @note If an attribute with the same key exists, it will be replaced
   */
  void SetAttribute(std::string key, std::shared_ptr<Attribute> val);

  /**
   * @brief Check if an attribute exists (copy version)
   * @param key Attribute key to check
   * @return true if attribute exists, false otherwise
   */
  bool HasAttribute(const std::string &key) noexcept;

  /**
   * @brief Check if an attribute exists (move version)
   * @param key Attribute key to check (will be moved)
   * @return true if attribute exists, false otherwise
   */
  bool HasAttribute(std::string &&key) noexcept;

  /**
   * @brief Construct an empty context
   */
  Context() = default;

 private:
  std::shared_mutex mtx_;  ///< Read-write lock for thread safety
  std::unordered_map<std::string, std::shared_ptr<Attribute>>
      map_;  ///< Attribute storage
};
}  // namespace bsrvcore

#endif
