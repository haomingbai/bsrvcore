/**
 * @file session_context_entry.h
 * @brief Session context container with expiration tracking
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-26
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Wraps a session context with its expiration time for use in SessionMap.
 * Provides thread-safe access to session data and expiration management.
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_SESSION_CONTEXT_ENTRY_H_
#define BSRVCORE_INTERNAL_SESSION_CONTEXT_ENTRY_H_

#include <chrono>
#include <memory>

#include "bsrvcore/context.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

namespace session_internal {

/**
 * @brief Container for session context with expiration time
 *
 * This class holds a session context and its expiration time, allowing
 * SessionMap to manage session lifecycle and automatic cleanup.
 *
 * @code
 * // Example usage in SessionMap
 * auto context = std::make_shared<Context>();
 * auto expiry = std::chrono::steady_clock::now() + std::chrono::hours(1);
 * SessionContextEntry entry(context, expiry);
 *
 * // Check if session is still valid
 * if (entry.GetExpiry() > std::chrono::steady_clock::now()) {
 *     auto ctx = entry.GetContext();  // Use the valid session
 * }
 * @endcode
 */
class SessionContextEntry : CopyableMovable<SessionContextEntry> {
 public:
  /**
   * @brief Get the session context
   * @return Shared pointer to the Context object
   */
  std::shared_ptr<Context> GetContext() const;

  /**
   * @brief Get the session expiration time
   * @return Time point when the session expires
   */
  std::chrono::steady_clock::time_point GetExpiry() const;

  /**
   * @brief Update the session expiration time
   * @param expiry New expiration time point
   *
   * @note This is used when extending session lifetime or setting custom
   * timeouts
   */
  void SetExpiry(std::chrono::steady_clock::time_point expiry);

  /**
   * @brief Construct a session context entry
   * @param context The session context to store
   * @param expiry Initial expiration time for the session
   */
  SessionContextEntry(std::shared_ptr<Context> context,
                      std::chrono::steady_clock::time_point expiry);

 private:
  std::shared_ptr<Context> ctx_;                  ///< Session context data
  std::chrono::steady_clock::time_point expiry_;  ///< Session expiration time
};

}  // namespace session_internal

}  // namespace bsrvcore

#endif
