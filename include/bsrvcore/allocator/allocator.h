/**
 * @file allocator.h
 * @brief Public allocator ABI for bsrvcore.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-21
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_ALLOCATOR_ALLOCATOR_H_
#define BSRVCORE_ALLOCATOR_ALLOCATOR_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace bsrvcore {

/**
 * @brief Allocate memory with optional alignment.
 *
 * @note This declaration is part of the public ABI.
 */
[[nodiscard]] void* Allocate(std::size_t size,
                             std::size_t alignment = alignof(std::max_align_t));

/**
 * @brief Deallocate memory allocated by Allocate().
 *
 * @note This declaration is part of the public ABI.
 */
void Deallocate(void* ptr, std::size_t size = 0,
                std::size_t alignment = alignof(std::max_align_t)) noexcept;

template <typename T>
class Allocator {
 public:
  using value_type = T;
  using is_always_equal = std::true_type;

  Allocator() noexcept = default;

  template <typename U>
  explicit Allocator(const Allocator<U>& /*unused*/) noexcept {}

  [[nodiscard]] T* allocate(std::size_t n) {
    if (n == 0) {
      return nullptr;
    }
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_array_new_length();
    }
    return static_cast<T*>(Allocate(sizeof(T) * n, alignof(T)));
  }

  void deallocate(T* ptr, std::size_t n) noexcept {
    if (ptr == nullptr) {
      return;
    }
    Deallocate(ptr, sizeof(T) * n, alignof(T));
  }

  template <typename U>
  struct rebind {
    using other = Allocator<U>;
  };

  template <typename U>
  friend bool operator==(const Allocator& /*unused*/,
                         const Allocator<U>& /*unused*/) noexcept {
    return true;
  }
};

/**
 * @brief Type-erased deleter for allocator-owned objects.
 *
 * One deleter type for all `OwnedPtr<T>` keeps base/derived pointer conversion
 * working while preserving concrete destruction logic.
 */
class OwnedDeleter {
 public:
  using DestroyFn = void (*)(void*) noexcept;

  OwnedDeleter() noexcept = default;
  explicit OwnedDeleter(DestroyFn fn) noexcept : fn_(fn) {}

  template <typename T>
  static OwnedDeleter ForType() noexcept {
    return OwnedDeleter{&DestroyImpl<T>};
  }

  template <typename T>
  void operator()(T* ptr) const noexcept {
    if (ptr == nullptr || fn_ == nullptr) {
      return;
    }
    fn_(static_cast<void*>(ptr));
  }

 private:
  template <typename T>
  static void DestroyImpl(void* raw) noexcept {
    auto* ptr = static_cast<T*>(raw);
    std::destroy_at(ptr);
    Deallocate(ptr, sizeof(T), alignof(T));
  }

  DestroyFn fn_{nullptr};
};

template <typename T>
using OwnedPtr = std::unique_ptr<T, OwnedDeleter>;

template <typename T, typename... Args>
[[nodiscard]] OwnedPtr<T> AllocateUnique(Args&&... args) {
  void* raw = Allocate(sizeof(T), alignof(T));
  try {
    T* obj = new (raw) T(std::forward<Args>(args)...);
    return OwnedPtr<T>(obj, OwnedDeleter::ForType<T>());
  } catch (...) {
    Deallocate(raw, sizeof(T), alignof(T));
    throw;
  }
}

template <typename T, typename... Args>
[[nodiscard]] std::shared_ptr<T> AllocateShared(Args&&... args) {
  return std::allocate_shared<T>(Allocator<T>{}, std::forward<Args>(args)...);
}

template <typename T, typename... Args>
[[nodiscard]] T* AllocateConstruct(Args&&... args) {
  void* raw = Allocate(sizeof(T), alignof(T));
  try {
    return new (raw) T(std::forward<Args>(args)...);
  } catch (...) {
    Deallocate(raw, sizeof(T), alignof(T));
    throw;
  }
}

template <typename T>
void DestroyDeallocate(T* ptr) noexcept {
  if (ptr == nullptr) {
    return;
  }
  std::destroy_at(ptr);
  Deallocate(ptr, sizeof(T), alignof(T));
}

}  // namespace bsrvcore

#endif  // BSRVCORE_ALLOCATOR_ALLOCATOR_H_
