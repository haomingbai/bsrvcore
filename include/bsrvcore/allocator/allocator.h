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
#include <deque>
#include <functional>
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
 * @param size Number of bytes to allocate.
 * @param alignment Required alignment in bytes.
 * @return Pointer to allocated storage.
 *
 * @note This declaration is part of the public ABI.
 */
[[nodiscard]] void* Allocate(std::size_t size,
                             std::size_t alignment = alignof(std::max_align_t));

/**
 * @brief Deallocate memory allocated by Allocate().
 *
 * @param ptr Pointer previously returned by Allocate(), or null.
 * @param size Number of bytes originally allocated when known.
 * @param alignment Alignment originally requested when known.
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
  /**
   * @brief Construct from allocator of another value type.
   *
   * @param other Source allocator; all bsrvcore allocators are interchangeable.
   */
  explicit Allocator(const Allocator<U>& other) noexcept {
    (void)other;
  }

  /**
   * @brief Allocate storage for `n` objects of type `T`.
   *
   * @param n Number of `T` objects to reserve raw storage for.
   * @return Pointer to uninitialized storage, or null when `n` is zero.
   */
  [[nodiscard]] T* allocate(std::size_t n) {
    if (n == 0) {
      return nullptr;
    }
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_array_new_length();
    }
    return static_cast<T*>(Allocate(sizeof(T) * n, alignof(T)));
  }

  /**
   * @brief Release storage previously obtained from allocate().
   *
   * @param ptr Pointer returned by allocate(), or null.
   * @param n Number of `T` objects originally allocated.
   */
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
  /** @brief Enable heterogeneous lookup for string-like keys. */
  using is_transparent = void;

  /**
   * @brief Hash a string view.
   *
   * @param value String view to hash.
   * @return Hash value for `value`.
   */
  std::size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }

  /**
   * @brief Hash a standard string.
   *
   * @param value Standard string to hash.
   * @return Hash value for `value`.
   */
  std::size_t operator()(const std::string& value) const noexcept {
    return (*this)(std::string_view{value});
  }

  /**
   * @brief Hash an allocator-backed string.
   *
   * @param value Allocator-backed string to hash.
   * @return Hash value for `value`.
   */
  std::size_t operator()(const AllocatedString& value) const noexcept {
    return (*this)(std::string_view{value});
  }
};

/**
 * @brief Transparent equality for string-like keys.
 */
struct TransparentStringEqual {
  /** @brief Enable heterogeneous equality for string-like keys. */
  using is_transparent = void;

  /**
   * @brief Compare two string-like values as string views.
   *
   * @param lhs Left value to compare.
   * @param rhs Right value to compare.
   * @return True when both values have identical string-view contents.
   */
  template <typename L, typename R>
    requires(std::is_convertible_v<const L&, std::string_view> &&
             std::is_convertible_v<const R&, std::string_view>)
  bool operator()(const L& lhs, const R& rhs) const noexcept {
    return std::string_view{lhs} == std::string_view{rhs};
  }
};

/**
 * @brief Copy a string view into an allocator-backed string.
 *
 * @param value String view to copy.
 * @return Allocator-backed string containing `value`.
 */
inline AllocatedString ToAllocatedString(std::string_view value) {
  return {value.begin(), value.end()};
}

/**
 * @brief Copy a string view into a standard string.
 *
 * @param value String view to copy.
 * @return Standard string containing `value`.
 */
inline std::string ToStdString(std::string_view value) {
  return {value.begin(), value.end()};
}

/**
 * @brief Copy a vector into allocator-backed storage.
 *
 * @param values Source vector.
 * @return Allocator-backed vector containing copied values.
 */
template <typename T, typename Alloc>
AllocatedVector<T> ToAllocatedVector(const std::vector<T, Alloc>& values) {
  return {values.begin(), values.end()};
}

/**
 * @brief Move or copy a vector into allocator-backed storage.
 *
 * @param values Source vector to move from when allocators differ.
 * @return Allocator-backed vector containing the source values.
 */
template <typename T, typename Alloc>
AllocatedVector<T> ToAllocatedVector(std::vector<T, Alloc>&& values) {
  if constexpr (std::is_same_v<Alloc, Allocator<T>>) {
    return AllocatedVector<T>(std::move(values));
  } else {
    AllocatedVector<T> out;
    out.reserve(values.size());
    for (auto& value : values) {
      out.emplace_back(std::move(value));
    }
    return out;
  }
}

/**
 * @brief Copy allocator-backed vector into standard storage.
 *
 * @param values Source allocator-backed vector.
 * @return Standard vector containing copied values.
 */
template <typename T>
std::vector<T> ToStdVector(const AllocatedVector<T>& values) {
  return {values.begin(), values.end()};
}

/**
 * @brief Move allocator-backed vector into standard storage.
 *
 * @param values Source allocator-backed vector to move from.
 * @return Standard vector containing moved values.
 */
template <typename T>
std::vector<T> ToStdVector(AllocatedVector<T>&& values) {
  std::vector<T> out;
  out.reserve(values.size());
  for (auto& value : values) {
    out.emplace_back(std::move(value));
  }
  return out;
}

/**
 * @brief Copy a deque into allocator-backed storage.
 *
 * @param values Source deque.
 * @return Allocator-backed deque containing copied values.
 */
template <typename T, typename Alloc>
AllocatedDeque<T> ToAllocatedDeque(const std::deque<T, Alloc>& values) {
  return {values.begin(), values.end()};
}

/**
 * @brief Move or copy a deque into allocator-backed storage.
 *
 * @param values Source deque to move from when allocators differ.
 * @return Allocator-backed deque containing the source values.
 */
template <typename T, typename Alloc>
AllocatedDeque<T> ToAllocatedDeque(std::deque<T, Alloc>&& values) {
  if constexpr (std::is_same_v<Alloc, Allocator<T>>) {
    return AllocatedDeque<T>(std::move(values));
  } else {
    AllocatedDeque<T> out;
    for (auto& value : values) {
      out.emplace_back(std::move(value));
    }
    return out;
  }
}

/**
 * @brief Copy allocator-backed deque into standard storage.
 *
 * @param values Source allocator-backed deque.
 * @return Standard deque containing copied values.
 */
template <typename T>
std::deque<T> ToStdDeque(const AllocatedDeque<T>& values) {
  return {values.begin(), values.end()};
}

/**
 * @brief Move allocator-backed deque into standard storage.
 *
 * @param values Source allocator-backed deque to move from.
 * @return Standard deque containing moved values.
 */
template <typename T>
std::deque<T> ToStdDeque(AllocatedDeque<T>&& values) {
  std::deque<T> out;
  for (auto& value : values) {
    out.emplace_back(std::move(value));
  }
  return out;
}

/**
 * @brief Copy an unordered map into allocator-backed storage.
 *
 * @param values Source unordered map.
 * @return Allocator-backed unordered map containing copied entries.
 */
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

/**
 * @brief Move an unordered map into allocator-backed storage.
 *
 * @param values Source unordered map to move entries from.
 * @return Allocator-backed unordered map containing moved entries.
 */
template <typename K, typename V, typename Hash, typename KeyEqual,
          typename Alloc>
AllocatedUnorderedMap<K, V, Hash, KeyEqual> ToAllocatedUnorderedMap(
    std::unordered_map<K, V, Hash, KeyEqual, Alloc>&& values) {
  AllocatedUnorderedMap<K, V, Hash, KeyEqual> out;
  out.reserve(values.size());
  while (!values.empty()) {
    auto node = values.extract(values.begin());
    out.emplace(std::move(node.key()), std::move(node.mapped()));
  }
  return out;
}

/**
 * @brief Copy allocator-backed unordered map into standard storage.
 *
 * @param values Source allocator-backed unordered map.
 * @return Standard unordered map containing copied entries.
 */
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

/**
 * @brief Move allocator-backed unordered map into standard storage.
 *
 * @param values Source allocator-backed unordered map to move entries from.
 * @return Standard unordered map containing moved entries.
 */
template <typename K, typename V, typename Hash, typename KeyEqual>
std::unordered_map<K, V, Hash, KeyEqual> ToStdUnorderedMap(
    AllocatedUnorderedMap<K, V, Hash, KeyEqual>&& values) {
  std::unordered_map<K, V, Hash, KeyEqual> out;
  out.reserve(values.size());
  while (!values.empty()) {
    auto node = values.extract(values.begin());
    out.emplace(std::move(node.key()), std::move(node.mapped()));
  }
  return out;
}

}  // namespace detail

/** @brief Allocator-backed string map with heterogeneous lookup support. */
using AllocatedStringMap =
    AllocatedUnorderedMap<AllocatedString, AllocatedString,
                          detail::TransparentStringHash,
                          detail::TransparentStringEqual>;

/** @brief Allocator-backed std::string-key map with heterogeneous lookup. */
template <typename V>
using AllocatedStdStringMap =
    AllocatedUnorderedMap<std::string, V, detail::TransparentStringHash,
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
  /**
   * @brief Construct a deleter from a raw destroy function.
   *
   * @param fn Function used to destroy and release a stored object.
   */
  explicit OwnedDeleter(DestroyFn fn) noexcept : fn_(fn) {}

  template <typename T>
  /**
   * @brief Create a deleter bound to a concrete object type.
   *
   * @return Deleter that destroys `T` and releases bsrvcore storage.
   */
  static OwnedDeleter ForType() noexcept {
    return OwnedDeleter{&DestroyImpl<T>};
  }

  template <typename T>
  /**
   * @brief Create a deleter that uses system `delete`.
   *
   * @return Deleter that destroys `T` through `delete`.
   */
  static OwnedDeleter ForDelete() noexcept {
    return OwnedDeleter{&DeleteImpl<T>};
  }

  template <typename T>
  /**
   * @brief Destroy and free one allocator-owned object.
   *
   * @param ptr Pointer to destroy, or null.
   */
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

/** @brief Unique pointer that releases memory through bsrvcore allocator ABI.
 */
template <typename T>
using OwnedPtr = std::unique_ptr<T, OwnedDeleter>;

/**
 * @brief Allocate and construct one object owned by `OwnedPtr`.
 *
 * @param args Constructor arguments forwarded to `T`.
 * @return Owned pointer with allocator-backed destruction.
 */
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

/**
 * @brief Allocate `Derived`, return as `OwnedPtr<Base>` with derived deleter.
 *
 * @param args Constructor arguments forwarded to `Derived`.
 * @return Owned pointer typed as `Base` and destroyed as `Derived`.
 */
template <typename Base, typename Derived, typename... Args>
  requires std::derived_from<Derived, Base>
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
 *
 * @param ptr System-owned pointer to adopt.
 * @return Owned pointer typed as `Base` and destroyed through `delete`.
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

/**
 * @brief Allocate and construct one shared object with bsrvcore allocator.
 *
 * @param args Constructor arguments forwarded to `T`.
 * @return Shared pointer to the constructed object.
 */
template <typename T, typename... Args>
[[nodiscard]] std::shared_ptr<T> AllocateShared(Args&&... args) {
  return std::allocate_shared<T>(Allocator<T>{}, std::forward<Args>(args)...);
}

/**
 * @brief Allocate raw storage and construct one object in-place.
 *
 * @param args Constructor arguments forwarded to `T`.
 * @return Raw pointer to the constructed object.
 */
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

/**
 * @brief Destroy one object and release its allocator-owned storage.
 *
 * @param ptr Pointer returned by AllocateConstruct(), or null.
 */
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
