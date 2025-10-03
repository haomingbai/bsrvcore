/**
 * @file session_map.h
 * @brief Session management with automatic expiration and cleanup
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-26
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Thread-safe session storage with automatic timeout-based cleanup.
 * Uses a min-heap for efficient expiration tracking and background cleaner.
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_SESSION_MAP_H_
#define BSRVCORE_INTERNAL_SESSION_MAP_H_

#include <atomic>
#include <boost/asio/steady_timer.hpp>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "bsrvcore/http_server_task.h"
#include "bsrvcore/internal/heap.h"
#include "bsrvcore/internal/session_context_entry.h"
#include "bsrvcore/internal/session_key_heap_entry.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {
class Context;
class HttpServer;

/**
 * @brief Manages HTTP sessions with automatic expiration and cleanup
 *
 * SessionMap provides thread-safe storage for session contexts with
 * configurable timeouts and background cleanup. Sessions are automatically
 * removed when they expire.
 *
 * @code
 * auto session_map = std::make_shared<SessionMap>(executor, http_server);
 * session_map->SetDefaultSessionTimeout(3600000); // 1 hour
 * session_map->SetBackgroundCleaner(true);
 * @endcode
 */
class SessionMap : NonCopyableNonMovable<SessionMap>,
                   public std::enable_shared_from_this<SessionMap> {
 public:
  /**
   * @brief Retrieve a session by ID (copy version)
   * @param sessionid The session identifier
   * @return Shared pointer to Context, nullptr if not found or expired
   */
  std::shared_ptr<Context> GetSession(const std::string &sessionid);

  /**
   * @brief Retrieve a session by ID (move version)
   * @param sessionid The session identifier (will be moved)
   * @return Shared pointer to Context, nullptr if not found or expired
   */
  std::shared_ptr<Context> GetSession(std::string &&sessionid);

  /**
   * @brief Remove a session by ID (copy version)
   * @param sessionid The session identifier to remove
   * @return true if session was found and removed, false otherwise
   */
  bool RemoveSession(const std::string &sessionid);

  /**
   * @brief Remove a session by ID (move version)
   * @param sessionid The session identifier to remove (will be moved)
   * @return true if session was found and removed, false otherwise
   */
  bool RemoveSession(std::string &&sessionid);

  /**
   * @brief Enable or disable background session cleanup
   * @param allow_cleaner true to enable automatic cleanup, false to disable
   */
  void SetBackgroundCleaner(bool allow_cleaner);

  /**
   * @brief Check if background cleaner is enabled
   * @return true if background cleanup is active
   */
  bool AllowBackgroundCleaner();

  /**
   * @brief Set cleanup interval in milliseconds
   * @param interval Cleanup interval in ms
   */
  void SetCleanerInterval(std::size_t interval) noexcept;

  /**
   * @brief Set default session timeout in milliseconds
   * @param timeout Default timeout in ms for new sessions
   */
  void SetDefaultSessionTimeout(std::size_t timeout) noexcept;

  /**
   * @brief Set custom timeout for a specific session (copy version)
   * @param sessionid The session identifier
   * @param timeout Custom timeout in milliseconds
   */
  void SetSessionTimeout(const std::string &sessionid, std::size_t timeout);

  /**
   * @brief Set custom timeout for a specific session (move version)
   * @param sessionid The session identifier (will be moved)
   * @param timeout Custom timeout in milliseconds
   */
  void SetSessionTimeout(std::string &&sessionid, std::size_t timeout);

  /**
   * @brief Construct a SessionMap
   * @tparam Executor Type of ASIO executor
   * @param exec ASIO executor for timer operations
   * @param server HTTP server instance
   *
   * @code
   * // Example construction
   * boost::asio::io_context io;
   * auto http_server = std::make_shared<HttpServer>();
   * auto session_map = std::make_shared<SessionMap>(io.get_executor(),
   * http_server);
   * @endcode
   */
  template <typename Executor>
  SessionMap(Executor exec, std::shared_ptr<HttpServer> server)
      : timer_(exec),
        server_(server),
        cleaner_interval_(1000 * 60 * 30),     // 30 minutes default
        default_timeout_(1000 * 60 * 60 * 2),  // 2 hours default
        allow_cleaner_(false) {}

 private:
  void SetCleaner();     ///< Initialize background cleanup timer
  void ShortClean();     ///< Quick cleanup of expired sessions
  void ThoroughClean();  ///< Comprehensive cleanup and memory reclaim

  std::mutex mtx_;  ///< Mutex for thread safety
  std::unordered_map<std::string, session_internal::SessionContextEntry>
      map_;  ///< Session storage
  Heap<session_internal::SessionKeyHeapEntry>
      pqueue_;                        ///< Priority queue for expiration
  boost::asio::steady_timer timer_;   ///< Cleanup timer
  std::weak_ptr<HttpServer> server_;  ///< Associated HTTP server
  std::atomic<std::size_t> cleaner_interval_;  ///< Cleanup interval in ms
  std::atomic<std::size_t> default_timeout_;  ///< Default session timeout in ms
  bool allow_cleaner_;                        ///< Cleaner enabled flag
};
}  // namespace bsrvcore

#endif
