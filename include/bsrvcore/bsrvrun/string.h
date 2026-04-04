/**
 * @file string.h
 * @brief ABI-oriented string type for bsrvrun plugin boundaries.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_STRING_H_
#define BSRVCORE_BSRVRUN_STRING_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "bsrvcore/core/trait.h"

namespace bsrvcore::bsrvrun {

/**
 * @brief Lightweight string type used for runtime config/plugin ABI.
 */
class String : public bsrvcore::CopyableMovable<String> {
 public:
  /** @brief Construct an empty string. */
  String() noexcept;
  /** @brief Construct from a null-terminated C string. */
  explicit String(const char* str);
  /** @brief Construct from raw character data and length. */
  String(const char* data, std::size_t size);
  /** @brief Construct from a string view. */
  explicit String(std::string_view view);

  /** @brief Copy-construct a string. */
  String(const String& other);
  /** @brief Move-construct a string. */
  String(String&& other) noexcept;
  /** @brief Copy-assign a string. */
  String& operator=(const String& other);
  /** @brief Move-assign a string. */
  String& operator=(String&& other) noexcept;

  /** @brief Destroy the string. */
  ~String();

  /** @brief Return the raw character pointer without guaranteeing NUL
   * termination. */
  [[nodiscard]] const char* Data() const noexcept;
  /** @brief Return the NUL-terminated character pointer. */
  [[nodiscard]] const char* CStr() const noexcept;
  /** @brief Return the string length in bytes. */
  [[nodiscard]] std::size_t Size() const noexcept;
  /** @brief Whether the string is empty. */
  [[nodiscard]] bool Empty() const noexcept;

  /** @brief Convert to `std::string`. */
  [[nodiscard]] std::string ToStdString() const;

 private:
  char* data_;
  std::size_t size_;
};

/** @brief Compare two ABI strings for equality. */
bool operator==(const String& lhs, const String& rhs) noexcept;
/** @brief Compare two ABI strings for inequality. */
bool operator!=(const String& lhs, const String& rhs) noexcept;

}  // namespace bsrvcore::bsrvrun

#endif
