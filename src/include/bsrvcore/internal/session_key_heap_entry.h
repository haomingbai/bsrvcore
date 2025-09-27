/**
 * @file session_key_heap_entry.h
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

#ifndef BSRVCORE_INTERNAL_SESSION_KEY_HEAP_ENTRY_H_
#define BSRVCORE_INTERNAL_SESSION_KEY_HEAP_ENTRY_H_

#include <chrono>
#include <string>

#include "bsrvcore/trait.h"

namespace bsrvcore {

namespace session_internal {

class SessionKeyHeapEntry : CopyableMovable<SessionKeyHeapEntry> {
 public:
  SessionKeyHeapEntry(std::string id,
                      std::chrono::steady_clock::time_point expiry);

  bool operator<(const SessionKeyHeapEntry &) const;

  std::chrono::steady_clock::time_point GetExpiry() const;

  const std::string &GetId() const;

 private:
  std::string id_;
  std::chrono::steady_clock::time_point expiry_;
};

}  // namespace session_internal

}  // namespace bsrvcore

#endif
