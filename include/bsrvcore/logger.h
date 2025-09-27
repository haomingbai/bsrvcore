/**
 * @file logger.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-24
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#ifndef BSRVCORE_LOGGER_H_
#define BSRVCORE_LOGGER_H_

#include <cstdint>
#include <string_view>

namespace bsrvcore {
enum class LogLevel : std::uint8_t {
  kTrace = 0,
  kDebug,
  kInfo,
  kWarn,
  kError,
  kFatal
};

class Logger {
 public:
  virtual void Log(LogLevel level, std::string_view message) = 0;

  virtual ~Logger() = delete;
};
}  // namespace bsrvcore

#endif
