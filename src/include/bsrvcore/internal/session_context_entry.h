/**
 * @file session_context_entry.h
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

#ifndef BSRVCORE_INTERNAL_SESSION_CONTEXT_ENTRY_H_
#define BSRVCORE_INTERNAL_SESSION_CONTEXT_ENTRY_H_

#include <chrono>
#include <memory>

#include "bsrvcore/context.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

namespace session_internal {

class SessionContextEntry : CopyableMovable<SessionContextEntry> {
 public:
  std::shared_ptr<Context> GetContext() const;

  std::chrono::steady_clock::time_point GetExpiry() const;

  void SetExpiry(std::chrono::steady_clock::time_point expiry);

  SessionContextEntry(std::shared_ptr<Context> context,
                      std::chrono::steady_clock::time_point expiry);

 private:
  std::shared_ptr<Context> ctx_;
  std::chrono::steady_clock::time_point expiry_;
};

}  // namespace session_internal

}  // namespace bsrvcore

#endif
