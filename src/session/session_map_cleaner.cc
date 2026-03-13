/**
 * @file session_map_cleaner.cc
 * @brief SessionMap expiration cleanup strategies.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * SessionMap uses a priority queue (pqueue_) that may contain stale entries
 * (e.g., when the same session id is pushed multiple times with newer expiry).
 * Therefore, during cleanup we only erase map_ when:
 * - the session id exists in map_, and
 * - the expiry timestamp equals the popped queue entry's expiry.
 */

#include "bsrvcore/internal/session_map.h"

#include <chrono>
#include <cstddef>

namespace {

constexpr std::size_t kMinShrinkSize = 256;

}  // namespace

namespace bsrvcore {

void SessionMap::ShortClean() {
  auto now = std::chrono::steady_clock::now();

  if (pqueue_.GetSize() > map_.size() * 2) {
    constexpr std::size_t max_clean_num = 8;
    std::size_t clean_cnt = 0;

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

}  // namespace bsrvcore
