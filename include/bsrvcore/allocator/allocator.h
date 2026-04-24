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

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

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

/** @brief `std::string` compatible type backed by bsrvcore allocator. */
using AllocatedString =
    std::basic_string<char, std::char_traits<char>, Allocator<char>>;

/** @brief `std::vector` compatible type backed by bsrvcore allocator. */
template <typename T>
using AllocatedVector = std::vector<T, Allocator<T>>;

/** @brief `std::deque` compatible type backed by bsrvcore allocator. */
template <typename T>
using AllocatedDeque = std::deque<T, Allocator<T>>;

/**
 * @brief `std::unordered_map` compatible type backed by bsrvcore allocator.
 */
template <typename K, typename V, typename Hash = std::hash<K>,
          typename KeyEqual = std::equal_to<K>>
using AllocatedUnorderedMap =
    std::unordered_map<K, V, Hash, KeyEqual, Allocator<std::pair<const K, V>>>;

namespace detail {

/**
 * @brief Transparent hash for string-like keys.
 */
struct TransparentStringHash {
  using is_transparent = void;

  std::size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }

  std::size_t operator()(const std::string& value) const noexcept {
    return (*this)(std::string_view{value});
  }

  std::size_t operator()(const AllocatedString& value) const noexcept {
    return (*this)(std::string_view{value});
  }
};

/**
 * @brief Transparent equality for string-like keys.
 */
struct TransparentStringEqual {
  using is_transparent = void;

  template <typename L, typename R>
    requires(std::is_convertible_v<const L&, std::string_view> &&
             std::is_convertible_v<const R&, std::string_view>)
  bool operator()(const L& lhs, const R& rhs) const noexcept {
    return std::string_view{lhs} == std::string_view{rhs};
  }
};

inline AllocatedString ToAllocatedString(std::string_view value) {
  return {value.begin(), value.end()};
}

inline std::string ToStdString(std::string_view value) {
  return {value.begin(), value.end()};
}

template <typename T, typename Alloc>
AllocatedVector<T> ToAllocatedVector(const std::vector<T, Alloc>& values) {
  return {values.begin(), values.end()};
}

template <typename T>
std::vector<T> ToStdVector(const AllocatedVector<T>& values) {
  return {values.begin(), values.end()};
}

template <typename T, typename Alloc>
AllocatedDeque<T> ToAllocatedDeque(const std::deque<T, Alloc>& values) {
  return {values.begin(), values.end()};
}

template <typename T>
std::deque<T> ToStdDeque(const AllocatedDeque<T>& values) {
  return {values.begin(), values.end()};
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          typename Alloc>
AllocatedUnorderedMap<K, V, Hash, KeyEqual> ToAllocatedUnorderedMap(
    const std::unordered_map<K, V, Hash, KeyEqual, Alloc>& values) {
  AllocatedUnorderedMap<K, V, Hash, KeyEqual> out;
  out.reserve(values.size());
  for (const auto& [key, value] : values) {
    out.emplace(key, value);
  }
  return out;
}

template <typename K, typename V, typename Hash, typename KeyEqual>
std::unordered_map<K, V, Hash, KeyEqual> ToStdUnorderedMap(
    const AllocatedUnorderedMap<K, V, Hash, KeyEqual>& values) {
  std::unordered_map<K, V, Hash, KeyEqual> out;
  out.reserve(values.size());
  for (const auto& [key, value] : values) {
    out.emplace(key, value);
  }
  return out;
}

}  // namespace detail

/** @brief Allocator-backed string map with heterogeneous lookup support. */
using AllocatedStringMap =
    AllocatedUnorderedMap<AllocatedString, AllocatedString,
                          detail::TransparentStringHash,
                          detail::TransparentStringEqual>;

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
  /** @brief Create a deleter that uses system `delete`. */
  static OwnedDeleter ForDelete() noexcept {
    return OwnedDeleter{&DeleteImpl<T>};
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

  template <typename T>
  static void DeleteImpl(void* raw) noexcept {
    delete static_cast<T*>(raw);
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

template <typename Base, typename Derived, typename... Args>
  requires std::derived_from<Derived, Base>
/** @brief Allocate `Derived`, return as `OwnedPtr<Base>` with derived deleter.
 */
[[nodiscard]] OwnedPtr<Base> AllocateUniqueAs(Args&&... args) {
  auto derived = AllocateUnique<Derived>(std::forward<Args>(args)...);
  return OwnedPtr<Base>(static_cast<Base*>(derived.release()),
                        OwnedDeleter::ForType<Derived>());
}

/**
 * @brief Adopt a system `std::unique_ptr<Derived>` into `OwnedPtr<Base>`.
 *
 * The adopted pointer is destroyed through system `delete` with `Derived`
 * static type, which preserves correct destruction when registered via
 * `std::make_unique`.
 */
template <typename Base, typename Derived>
  requires std::derived_from<Derived, Base>
[[nodiscard]] OwnedPtr<Base> AdoptUniqueAs(
    std::unique_ptr<Derived> ptr) noexcept {
  if (!ptr) {
    return OwnedPtr<Base>(nullptr, OwnedDeleter::ForDelete<Derived>());
  }

  return OwnedPtr<Base>(static_cast<Base*>(ptr.release()),
                        OwnedDeleter::ForDelete<Derived>());
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
