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

namespace bsrvcore::bsrvrun {

/**
 * @brief Lightweight string type used for runtime config/plugin ABI.
 */
class String {
 public:
  String() noexcept;
  explicit String(const char* str);
  String(const char* data, std::size_t size);
  explicit String(std::string_view view);

  String(const String& other);
  String(String&& other) noexcept;
  String& operator=(const String& other);
  String& operator=(String&& other) noexcept;

  ~String();

  [[nodiscard]] const char* Data() const noexcept;
  [[nodiscard]] const char* CStr() const noexcept;
  [[nodiscard]] std::size_t Size() const noexcept;
  [[nodiscard]] bool Empty() const noexcept;

  [[nodiscard]] std::string ToStdString() const;

 private:
  char* data_;
  std::size_t size_;
};

bool operator==(const String& lhs, const String& rhs) noexcept;
bool operator!=(const String& lhs, const String& rhs) noexcept;

}  // namespace bsrvcore::bsrvrun

#endif
