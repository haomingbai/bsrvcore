/**
 * @file empty_logger.h
 * @brief No-op logger implementation used as a default internal fallback.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides an internal Logger implementation that discards all log messages.
 */

#include <string>

#include "bsrvcore/core/logger.h"

namespace bsrvcore {

namespace internal {

/**
 * @brief A Logger implementation that discards all messages.
 *
 * Intended for internal use when no user-provided logger is configured.
 */
class EmptyLogger : public Logger {
 public:
  /**
   * @brief Discard a log entry.
   * @param level Log severity.
   * @param log Log message.
   */
  void Log(LogLevel level, std::string log) override;
};

}  // namespace internal

}  // namespace bsrvcore
