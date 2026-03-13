/**
 * @file empty_logger.cc
 * @brief EmptyLogger implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Discards log messages when no real logger is configured.
 */

#include "bsrvcore/internal/empty_logger.h"

#include <string>

#include "bsrvcore/logger.h"

void bsrvcore::internal::EmptyLogger::Log(
    [[maybe_unused]] bsrvcore::LogLevel level,
    [[maybe_unused]] std::string log) {
  return;
}
