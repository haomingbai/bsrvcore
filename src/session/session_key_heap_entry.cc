/**
 * @file session_key_heap_entry.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-27
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/internal/session_key_heap_entry.h"

#include <chrono>
#include <string>
#include <utility>

using namespace bsrvcore::session_internal;

SessionKeyHeapEntry::SessionKeyHeapEntry(
    std::string id, std::chrono::steady_clock::time_point expiry)
    : id_(std::move(id)), expiry_(expiry) {}

bool SessionKeyHeapEntry::operator<(const SessionKeyHeapEntry &other) const {
  return expiry_ > other.expiry_;
}
