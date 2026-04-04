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

#include "bsrvcore/core/trait.h"

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
/**
 * @brief STL-compatible allocator backed by bsrvcore's public allocation ABI.
 *
 * @tparam T Element type allocated by the allocator.
 */
class Allocator : public CopyableMovable<Allocator<T>> {
 public:
  /** @brief Element type allocated by this allocator. */
  using value_type = T;
  /** @brief Signals that all instances compare equal. */
  using is_always_equal = std::true_type;

  /** @brief Construct an allocator instance. */
  Allocator() noexcept = default;

  template <typename U>
  /** @brief Construct from allocator of another value type. */
  explicit Allocator(const Allocator<U>& /*unused*/) noexcept {}

  /** @brief Allocate storage for `n` objects of type `T`. */
  [[nodiscard]] T* allocate(std::size_t n) {
    if (n == 0) {
      return nullptr;
    }
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_array_new_length();
    }
    return static_cast<T*>(Allocate(sizeof(T) * n, alignof(T)));
  }

  /** @brief Release storage previously obtained from allocate(). */
  void deallocate(T* ptr, std::size_t n) noexcept {
    if (ptr == nullptr) {
      return;
    }
    Deallocate(ptr, sizeof(T) * n, alignof(T));
  }

  template <typename U>
  /** @brief Rebind this allocator to another value type. */
  struct rebind {
    /** @brief Allocator rebound to `U`. */
    using other = Allocator<U>;
  };

  template <typename U>
  /** @brief Allocators backed by the same ABI always compare equal. */
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
class OwnedDeleter : public CopyableMovable<OwnedDeleter> {
 public:
  /** @brief Function pointer used to destroy and deallocate one object. */
  using DestroyFn = void (*)(void*) noexcept;

  /** @brief Construct an empty deleter. */
  OwnedDeleter() noexcept = default;
  /** @brief Construct a deleter from a raw destroy function. */
  explicit OwnedDeleter(DestroyFn fn) noexcept : fn_(fn) {}

  template <typename T>
  /** @brief Create a deleter bound to a concrete object type. */
  static OwnedDeleter ForType() noexcept {
    return OwnedDeleter{&DestroyImpl<T>};
  }

  template <typename T>
  /** @brief Destroy and free one allocator-owned object. */
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
/** @brief Unique pointer that releases memory through bsrvcore allocator ABI.
 */
using OwnedPtr = std::unique_ptr<T, OwnedDeleter>;

template <typename T, typename... Args>
/** @brief Allocate and construct one object owned by `OwnedPtr`. */
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
/** @brief Allocate and construct one shared object with bsrvcore allocator. */
[[nodiscard]] std::shared_ptr<T> AllocateShared(Args&&... args) {
  return std::allocate_shared<T>(Allocator<T>{}, std::forward<Args>(args)...);
}

template <typename T, typename... Args>
/** @brief Allocate raw storage and construct one object in-place. */
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
/** @brief Destroy one object and release its allocator-owned storage. */
void DestroyDeallocate(T* ptr) noexcept {
  if (ptr == nullptr) {
    return;
  }
  std::destroy_at(ptr);
  Deallocate(ptr, sizeof(T), alignof(T));
}

}  // namespace bsrvcore

#endif  // BSRVCORE_ALLOCATOR_ALLOCATOR_H_
