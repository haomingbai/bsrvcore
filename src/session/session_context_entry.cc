/**
 * @file session_context_entry.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-27
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/internal/session_context_entry.h"

#include <chrono>
#include <memory>

#include "bsrvcore/context.h"

using namespace bsrvcore::session_internal;

void SessionContextEntry::SetExpiry(
    std::chrono::steady_clock::time_point expiry) {
  expiry_ = expiry;
}

std::shared_ptr<bsrvcore::Context> SessionContextEntry::GetContext() const {
  return ctx_;
}

std::chrono::steady_clock::time_point SessionContextEntry::GetExpiry() const {
  return expiry_;
}

SessionContextEntry::SessionContextEntry(
    std::shared_ptr<Context> context,
    std::chrono::steady_clock::time_point expiry)
    : ctx_(context), expiry_(expiry) {}
