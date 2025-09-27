/**
 * @file session_map.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-26
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
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

class SessionMap : NonCopyableNonMovable<SessionMap>,
                   std::enable_shared_from_this<SessionMap> {
 public:
  std::shared_ptr<Context> GetSession(const std::string &sessionid);

  std::shared_ptr<Context> GetSession(std::string &&sessionid);

  bool RemoveSession(const std::string &sessionid);

  bool RemoveSession(std::string &&sessionid);

  void SetBackgroundCleaner(bool allow_cleaner);

  bool AllowBackgroundCleaner();

  void SetCleanerInterval(std::size_t interval);

  void SetDefaultSessionTimeout(std::size_t timeout);

  void SetSessionTimeout(const std::string &sessionid, std::size_t timeout);

  void SetSessionTimeout(std::string &&sessionid, std::size_t timeout);

  template <typename Executor>
  SessionMap(Executor exec, std::shared_ptr<HttpServer> server)
      : timer_(exec),
        server_(server),
        cleaner_interval_(1000 * 60 * 30),
        default_timeout_(1000 * 60 * 60 * 2),
        allow_cleaner_(false) {}

 private:
  void SetCleaner();

  void ShortClean();

  void ThoroughClean();

  std::mutex mtx_;
  std::unordered_map<std::string, session_internal::SessionContextEntry> map_;
  Heap<session_internal::SessionKeyHeapEntry> pqueue_;
  boost::asio::steady_timer timer_;
  std::weak_ptr<HttpServer> server_;
  std::atomic<std::size_t> cleaner_interval_;
  std::atomic<std::size_t> default_timeout_;
  bool allow_cleaner_;
};
}  // namespace bsrvcore

#endif
