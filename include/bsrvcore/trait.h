/**
 * @file trait.h
 * @brief Compiler-friendly traits for controlling copy and move semantics
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-26
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides CRTP-based traits to explicitly control copy and move semantics
 * of derived classes. These traits make intention explicit and prevent
 * accidental copies or moves that could break class invariants.
 */

#pragma once

#ifndef BSRVCORE_TRAIT_H_
#define BSRVCORE_TRAIT_H_

namespace bsrvcore {

/**
 * @brief Enables copy semantics only, disables move semantics
 *
 * Use this trait when a class should be copyable but not movable.
 * This is useful for classes that manage resources where moving
 * could invalidate internal pointers or references.
 *
 * @tparam Derived The class inheriting from this trait (CRTP)
 *
 * @code
 * class MyCopyableClass : public CopyableOnly<MyCopyableClass> {
 *   // This class can be copied but not moved
 *   // Copy constructor and copy assignment are automatically enabled
 *   // Move constructor and move assignment are explicitly disabled
 * };
 *
 * MyCopyableClass a;
 * MyCopyableClass b = a;        // OK - copy
 * MyCopyableClass c = std::move(a); // Compile error - move disabled
 * @endcode
 */
template <typename Derived>
struct CopyableOnly {
  CopyableOnly() = default;
  CopyableOnly(const CopyableOnly&) = default;             // allow copy
  CopyableOnly& operator=(const CopyableOnly&) = default;  // allow copy-assign

  CopyableOnly(CopyableOnly&&) = delete;  // disallow move
  CopyableOnly& operator=(CopyableOnly&&) = delete;

  ~CopyableOnly() = default;

 protected:
  /// @brief CRTP helper to access derived class instance
  Derived& derived() noexcept { return static_cast<Derived&>(*this); }

  /// @brief CRTP helper to access const derived class instance
  const Derived& derived() const noexcept {
    return static_cast<const Derived&>(*this);
  }
};

/**
 * @brief Enables move semantics only, disables copy semantics
 *
 * Use this trait when a class should be movable but not copyable.
 * This is common for resource-managing classes like unique pointers,
 * file handles, or network connections where copying doesn't make sense.
 *
 * @tparam Derived The class inheriting from this trait (CRTP)
 *
 * @code
 * class MyMovableClass : public MovableOnly<MyMovableClass> {
 *   // This class can be moved but not copied
 *   // Move constructor and move assignment are automatically enabled
 *   // Copy constructor and copy assignment are explicitly disabled
 * };
 *
 * MyMovableClass a;
 * MyMovableClass b = std::move(a); // OK - move
 * MyMovableClass c = a;            // Compile error - copy disabled
 * @endcode
 */
template <typename Derived>
struct MovableOnly {
  MovableOnly() = default;

  MovableOnly(const MovableOnly&) = delete;  // disallow copy
  MovableOnly& operator=(const MovableOnly&) = delete;

  MovableOnly(MovableOnly&&) noexcept = default;  // allow move
  MovableOnly& operator=(MovableOnly&&) noexcept =
      default;  // allow move-assign

  ~MovableOnly() = default;

 protected:
  /// @brief CRTP helper to access derived class instance
  Derived& derived() noexcept { return static_cast<Derived&>(*this); }

  /// @brief CRTP helper to access const derived class instance
  const Derived& derived() const noexcept {
    return static_cast<const Derived&>(*this);
  }
};

/**
 * @brief Enables both copy and move semantics
 *
 * Use this trait to explicitly enable both copy and move operations.
 * This makes the class's semantics clear and ensures consistent behavior
 * even if the derived class adds complex member variables.
 *
 * @tparam Derived The class inheriting from this trait (CRTP)
 *
 * @code
 * class MyFlexibleClass : public CopyableMovable<MyFlexibleClass> {
 *   // This class can be both copied and moved
 *   // All copy and move operations are automatically enabled
 * };
 *
 * MyFlexibleClass a;
 * MyFlexibleClass b = a;            // OK - copy
 * MyFlexibleClass c = std::move(a); // OK - move
 * @endcode
 */
template <typename Derived>
struct CopyableMovable {
  CopyableMovable() = default;

  CopyableMovable(const CopyableMovable&) = default;  // allow copy
  CopyableMovable& operator=(const CopyableMovable&) = default;

  CopyableMovable(CopyableMovable&&) noexcept = default;  // allow move
  CopyableMovable& operator=(CopyableMovable&&) noexcept = default;

  ~CopyableMovable() = default;

 protected:
  /// @brief CRTP helper to access derived class instance
  Derived& derived() noexcept { return static_cast<Derived&>(*this); }

  /// @brief CRTP helper to access const derived class instance
  const Derived& derived() const noexcept {
    return static_cast<const Derived&>(*this);
  }
};

/**
 * @brief Disables both copy and move semantics
 *
 * Use this trait when a class should be neither copyable nor movable.
 * This is essential for singleton patterns, resource managers with
 * unique ownership, or classes that maintain internal state that
 * would be invalid if copied or moved.
 *
 * @tparam Derived The class inheriting from this trait (CRTP)
 *
 * @code
 * class MyUniqueClass : public NonCopyableNonMovable<MyUniqueClass> {
 *   // This class cannot be copied or moved
 *   // All copy and move operations are explicitly disabled
 * };
 *
 * MyUniqueClass a;
 * MyUniqueClass b = a;            // Compile error - copy disabled
 * MyUniqueClass c = std::move(a); // Compile error - move disabled
 * @endcode
 */
template <typename Derived>
struct NonCopyableNonMovable {
  NonCopyableNonMovable() = default;

  NonCopyableNonMovable(const NonCopyableNonMovable&) = delete;  // disable both
  NonCopyableNonMovable& operator=(const NonCopyableNonMovable&) = delete;

  NonCopyableNonMovable(NonCopyableNonMovable&&) = delete;
  NonCopyableNonMovable& operator=(NonCopyableNonMovable&&) = delete;

  ~NonCopyableNonMovable() = default;

 protected:
  /// @brief CRTP helper to access derived class instance
  Derived& derived() noexcept { return static_cast<Derived&>(*this); }

  /// @brief CRTP helper to access const derived class instance
  const Derived& derived() const noexcept {
    return static_cast<const Derived&>(*this);
  }
};

}  // namespace bsrvcore

#endif
