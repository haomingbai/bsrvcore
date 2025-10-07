/**
 * @file session_map.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-27
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/internal/session_map.h"

#include <algorithm>
#include <boost/system/detail/error_code.hpp>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

#include "bsrvcore/context.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/internal/session_context_entry.h"
#include "bsrvcore/internal/session_key_heap_entry.h"

using bsrvcore::SessionMap;

constexpr size_t kMinSessionTimeout = 1000;
constexpr size_t kMinShrinkSize = 256;

std::shared_ptr<bsrvcore::Context> SessionMap::GetSession(
    const std::string& sessionid) {
  using bsrvcore::session_internal::SessionContextEntry;

  std::lock_guard<std::mutex> lock(this->mtx_);
  std::shared_ptr<Context> result;

  auto now = std::chrono::steady_clock::now();

  if (map_.count(sessionid) && map_.at(sessionid).GetExpiry() > now) {
    auto& map_entry = map_.at(sessionid);
    result = map_entry.GetContext();
    auto new_expiry =
        std::max(now + std::chrono::milliseconds(default_timeout_),
                 map_entry.GetExpiry());

    if (new_expiry != map_entry.GetExpiry()) {
      pqueue_.Push(sessionid, new_expiry);
    }

    map_entry.SetExpiry(new_expiry);
  } else {
    result = std::make_shared<Context>();
    auto new_expiry = now + std::chrono::milliseconds(std::max<size_t>(
                                kMinSessionTimeout, default_timeout_));

    map_.emplace(sessionid, SessionContextEntry(result, new_expiry));

    pqueue_.Push(sessionid, new_expiry);
  }

  ShortClean();

  return result;
}

std::shared_ptr<bsrvcore::Context> SessionMap::GetSession(
    std::string&& sessionid) {
  using bsrvcore::session_internal::SessionContextEntry;

  std::lock_guard<std::mutex> lock(this->mtx_);
  std::shared_ptr<Context> result;

  auto now = std::chrono::steady_clock::now();

  if (map_.count(sessionid) && map_.at(sessionid).GetExpiry() > now) {
    auto& map_entry = map_.at(sessionid);
    result = map_entry.GetContext();
    auto new_expiry =
        std::max(now + std::chrono::milliseconds(default_timeout_),
                 map_entry.GetExpiry());

    if (new_expiry != map_entry.GetExpiry()) {
      pqueue_.Push(sessionid, new_expiry);
    }

    map_entry.SetExpiry(new_expiry);
  } else {
    result = std::make_shared<Context>();
    auto new_expiry = now + std::chrono::milliseconds(std::max<size_t>(
                                kMinSessionTimeout, default_timeout_));

    map_.emplace(sessionid, SessionContextEntry(result, new_expiry));

    pqueue_.Push(sessionid, new_expiry);
  }

  ShortClean();

  return result;
}

bool SessionMap::RemoveSession(const std::string& sessionid) {
  std::lock_guard<std::mutex> lock(mtx_);

  bool success;

  {
    auto it = map_.find(sessionid);

    if (it != map_.end()) {
      map_.erase(it);
      success = true;
    } else {
      success = false;
    }
  }

  ShortClean();

  return success;
}

bool SessionMap::RemoveSession(std::string&& sessionid) {
  std::lock_guard<std::mutex> lock(mtx_);

  bool success;

  {
    auto it = map_.find(sessionid);

    if (it != map_.end()) {
      map_.erase(it);
      success = true;
    } else {
      success = false;
    }
  }

  ShortClean();

  return success;
}

void SessionMap::SetBackgroundCleaner(bool allow_cleaner) {
  std::lock_guard<std::mutex> lock(mtx_);

  if (allow_cleaner == allow_cleaner_) {
    return;
  }

  allow_cleaner_ = allow_cleaner;

  if (allow_cleaner) {
    SetCleaner();
  } else {
    timer_.cancel();
  }
}

void SessionMap::SetCleaner() {
  if (!allow_cleaner_) {
    return;
  }

  timer_.expires_after(std::max<std::chrono::milliseconds>(
      std::chrono::milliseconds(kMinSessionTimeout),
      std::chrono::milliseconds(cleaner_interval_)));
  timer_.async_wait([this](boost::system::error_code ec) {
    if (ec) {
      return;
    }

    HttpServer* server_ptr = server_;
    if (server_ptr != nullptr && server_ptr->IsRunning()) {
      server_ptr->Post([this] {
        std::lock_guard<std::mutex> lock(mtx_);

        if (pqueue_.GetSize() < map_.size() * 8) {
          ShortClean();
        } else {
          ThoroughClean();
        }

        auto server_ptr = server_;

        if (allow_cleaner_ && server_ptr != nullptr &&
            server_ptr->IsRunning()) {
          SetCleaner();
        }
      });
    }
  });
}

void SessionMap::SetCleanerInterval(std::size_t interval) noexcept {
  cleaner_interval_ = interval;
}

void SessionMap::SetDefaultSessionTimeout(std::size_t timeout) noexcept {
  default_timeout_ = timeout;
}

void SessionMap::SetSessionTimeout(const std::string& sessionid,
                                   std::size_t timeout) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto now = std::chrono::steady_clock::now();

  auto it = map_.find(sessionid);

  if (it != map_.end()) {
    auto new_expiry = std::max(
        it->second.GetExpiry(),
        now + std::chrono::milliseconds(std::max(kMinSessionTimeout, timeout)));

    if (new_expiry != it->second.GetExpiry()) {
      pqueue_.Push(it->first, new_expiry);
      it->second.SetExpiry(new_expiry);
    }
  } else {
    auto result = std::make_shared<Context>();
    auto new_expiry =
        now + std::chrono::milliseconds(std::max(kMinSessionTimeout, timeout));

    map_.emplace(sessionid,
                 session_internal::SessionContextEntry{result, new_expiry});
    pqueue_.Push(sessionid, new_expiry);
  }

  ShortClean();
}

void SessionMap::SetSessionTimeout(std::string&& sessionid,
                                   std::size_t timeout) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto now = std::chrono::steady_clock::now();

  auto it = map_.find(sessionid);

  if (it != map_.end()) {
    auto new_expiry = std::max(
        it->second.GetExpiry(),
        now + std::chrono::milliseconds(std::max(kMinSessionTimeout, timeout)));

    if (new_expiry != it->second.GetExpiry()) {
      pqueue_.Push(it->first, new_expiry);
      it->second.SetExpiry(new_expiry);
    }
  } else {
    auto result = std::make_shared<Context>();
    auto new_expiry =
        now + std::chrono::milliseconds(std::max(kMinSessionTimeout, timeout));

    map_.emplace(sessionid,
                 session_internal::SessionContextEntry{result, new_expiry});
    pqueue_.Push(sessionid, new_expiry);
  }

  ShortClean();
}

void SessionMap::ShortClean() {
  auto now = std::chrono::steady_clock::now();

  if (pqueue_.GetSize() > map_.size() * 2) {
    constexpr std::size_t max_clean_num = 8;
    size_t clean_cnt = 0;

    while (clean_cnt < max_clean_num && !pqueue_.IsEmpty() &&
           pqueue_.Top().GetExpiry() <= now) {
      auto key_entry = pqueue_.Pop();

      auto it = map_.find(key_entry.GetId());
      if (it != map_.end() && it->second.GetExpiry() == key_entry.GetExpiry()) {
        map_.erase(it);
      }

      clean_cnt++;
    }

    if (pqueue_.GetSize() > kMinShrinkSize &&
        pqueue_.GetCapacity() > pqueue_.GetSize() * 8) {
      pqueue_.ShrinkToFit();
    }
  }
}

void SessionMap::ThoroughClean() {
  auto now = std::chrono::steady_clock::now();

  while (!pqueue_.IsEmpty() && pqueue_.Top().GetExpiry() <= now) {
    auto key_entry = pqueue_.Pop();

    auto it = map_.find(key_entry.GetId());
    if (it != map_.end() && it->second.GetExpiry() == key_entry.GetExpiry()) {
      map_.erase(it);
    }
  }

  if (pqueue_.GetSize() > kMinShrinkSize &&
      pqueue_.GetCapacity() > pqueue_.GetSize() * 8) {
    pqueue_.ShrinkToFit();
  }
}
