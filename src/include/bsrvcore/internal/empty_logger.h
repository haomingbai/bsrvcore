/**
 * @file empty_logger.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include <string>

#include "bsrvcore/logger.h"

namespace bsrvcore {

namespace internal {

class EmptyLogger : public Logger {
  void Log(LogLevel level, std::string log) override;
};

}  // namespace internal

}  // namespace bsrvcore
