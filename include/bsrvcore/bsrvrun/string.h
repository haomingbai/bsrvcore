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
  /**
   * @brief Construct from a null-terminated C string.
   *
   * @param str Source C string; null is treated as an empty string.
   */
  explicit String(const char* str);
  /**
   * @brief Construct from raw character data and length.
   *
   * @param data Source character data.
   * @param size Number of bytes to copy from `data`.
   */
  String(const char* data, std::size_t size);
  /**
   * @brief Construct from a string view.
   *
   * @param view Source string view to copy.
   */
  explicit String(std::string_view view);

  /**
   * @brief Copy-construct a string.
   *
   * @param other Source string to copy.
   */
  String(const String& other);
  /**
   * @brief Move-construct a string.
   *
   * @param other Source string to move from.
   */
  String(String&& other) noexcept;
  /**
   * @brief Copy-assign a string.
   *
   * @param other Source string to copy.
   * @return Reference to this string.
   */
  String& operator=(const String& other);
  /**
   * @brief Move-assign a string.
   *
   * @param other Source string to move from.
   * @return Reference to this string.
   */
  String& operator=(String&& other) noexcept;

  /** @brief Destroy the string. */
  ~String();

  /**
   * @brief Return the raw character pointer without guaranteeing NUL
   * termination.
   *
   * @return Pointer to the string data.
   */
  [[nodiscard]] const char* Data() const noexcept;
  /**
   * @brief Return the NUL-terminated character pointer.
   *
   * @return Pointer to a NUL-terminated buffer.
   */
  [[nodiscard]] const char* CStr() const noexcept;
  /**
   * @brief Return the string length in bytes.
   *
   * @return Byte length of the string.
   */
  [[nodiscard]] std::size_t Size() const noexcept;
  /**
   * @brief Whether the string is empty.
   *
   * @return True when Size() is zero.
   */
  [[nodiscard]] bool Empty() const noexcept;

  /**
   * @brief Convert to `std::string`.
   *
   * @return Standard string copy.
   */
  [[nodiscard]] std::string ToStdString() const;

 private:
  char* data_;
  std::size_t size_;
};

/**
 * @brief Compare two ABI strings for equality.
 *
 * @param lhs Left string.
 * @param rhs Right string.
 * @return True when both strings contain the same bytes.
 */
bool operator==(const String& lhs, const String& rhs) noexcept;
/**
 * @brief Compare two ABI strings for inequality.
 *
 * @param lhs Left string.
 * @param rhs Right string.
 * @return True when the strings differ.
 */
bool operator!=(const String& lhs, const String& rhs) noexcept;

}  // namespace bsrvcore::bsrvrun

#endif
