/**
 * @file string.cc
 * @brief Implementation of ABI-oriented bsrvcore::bsrvrun::String.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/bsrvrun/string.h"

#include <cstring>
#include <utility>

#include "bsrvcore/allocator.h"

namespace bsrvcore::bsrvrun {

namespace {

char* DuplicateBuffer(const char* data, std::size_t size) {
  auto* copied = static_cast<char*>(
      bsrvcore::Allocate(size + 1, alignof(char)));
  if (size > 0) {
    std::memcpy(copied, data, size);
  }
  copied[size] = '\0';
  return copied;
}

}  // namespace

String::String() noexcept : data_(DuplicateBuffer("", 0)), size_(0) {}

String::String(const char* str) : data_(nullptr), size_(0) {
  if (str == nullptr) {
    data_ = DuplicateBuffer("", 0);
    return;
  }

  size_ = std::strlen(str);
  data_ = DuplicateBuffer(str, size_);
}

String::String(const char* data, std::size_t size) : data_(nullptr), size_(size) {
  if (data == nullptr && size != 0) {
    size_ = 0;
    data_ = DuplicateBuffer("", 0);
    return;
  }

  data_ = DuplicateBuffer(data == nullptr ? "" : data, size_);
}

String::String(std::string_view view)
    : data_(DuplicateBuffer(view.data(), view.size())), size_(view.size()) {}

String::String(const String& other)
    : data_(DuplicateBuffer(other.data_, other.size_)), size_(other.size_) {}

String::String(String&& other) noexcept : data_(other.data_), size_(other.size_) {
  other.data_ = DuplicateBuffer("", 0);
  other.size_ = 0;
}

String& String::operator=(const String& other) {
  if (this == &other) {
    return *this;
  }

  char* copied = DuplicateBuffer(other.data_, other.size_);
  bsrvcore::Deallocate(data_, size_ + 1, alignof(char));
  data_ = copied;
  size_ = other.size_;
  return *this;
}

String& String::operator=(String&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  bsrvcore::Deallocate(data_, size_ + 1, alignof(char));
  data_ = other.data_;
  size_ = other.size_;
  other.data_ = DuplicateBuffer("", 0);
  other.size_ = 0;
  return *this;
}

String::~String() { bsrvcore::Deallocate(data_, size_ + 1, alignof(char)); }

const char* String::Data() const noexcept { return data_; }

const char* String::CStr() const noexcept { return data_; }

std::size_t String::Size() const noexcept { return size_; }

bool String::Empty() const noexcept { return size_ == 0; }

std::string String::ToStdString() const { return std::string(data_, size_); }

bool operator==(const String& lhs, const String& rhs) noexcept {
  if (lhs.Size() != rhs.Size()) {
    return false;
  }

  return std::memcmp(lhs.Data(), rhs.Data(), lhs.Size()) == 0;
}

bool operator!=(const String& lhs, const String& rhs) noexcept {
  return !(lhs == rhs);
}

}  // namespace bsrvcore::bsrvrun
