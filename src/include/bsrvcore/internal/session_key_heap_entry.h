/**
 * @file session_key_heap_entry.h
 * @brief Heap entry for session expiration management
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-26
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Represents a session entry in the expiration heap. Used by SessionMap
 * to efficiently track and clean up expired sessions based on timeout.
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_SESSION_KEY_HEAP_ENTRY_H_
#define BSRVCORE_INTERNAL_SESSION_KEY_HEAP_ENTRY_H_

#include <chrono>
#include <string>

#include "bsrvcore/trait.h"

namespace bsrvcore {

namespace session_internal {

/**
 * @brief Entry in session expiration heap for timeout management
 *
 * This class represents a session entry in a min-heap used by SessionMap
 * to efficiently find and remove expired sessions. The heap is ordered
 * by expiration time, allowing quick access to the next session to expire.
 *
 * @code
 * // Example usage in SessionMap
 * auto expiry = std::chrono::steady_clock::now() + std::chrono::hours(1);
 * SessionKeyHeapEntry entry("session123", expiry);
 * heap.push(entry);  // Add to expiration heap
 * @endcode
 */
class SessionKeyHeapEntry : CopyableMovable<SessionKeyHeapEntry> {
 public:
  /**
   * @brief Construct a heap entry with session ID and expiration time
   * @param id Session identifier
   * @param expiry Absolute time when the session expires
   */
  SessionKeyHeapEntry(std::string id,
                      std::chrono::steady_clock::time_point expiry);

  /**
   * @brief Compare entries by expiration time (for min-heap ordering)
   * @param other The other entry to compare with
   * @return true if this entry expires earlier than the other
   *
   * @note This creates a min-heap where the earliest expiration is at the top
   */
  bool operator<(const SessionKeyHeapEntry &other) const noexcept;

  /**
   * @brief Get the expiration time of this session
   * @return Time point when the session expires
   */
  std::chrono::steady_clock::time_point GetExpiry() const noexcept;

  /**
   * @brief Get the session identifier
   * @return Constant reference to the session ID string
   */
  const std::string &GetId() const noexcept;

 private:
  std::string id_;                                ///< Session identifier
  std::chrono::steady_clock::time_point expiry_;  ///< Expiration time point
};

}  // namespace session_internal

}  // namespace bsrvcore

#endif
