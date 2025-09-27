/**
 * @file trait.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-26
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#ifndef BSRVCORE_TRAIT_H_
#define BSRVCORE_TRAIT_H_

namespace bsrvcore {
template <typename Derived>
struct CopyableOnly {
  CopyableOnly() = default;
  CopyableOnly(const CopyableOnly&) = default;             // allow copy
  CopyableOnly& operator=(const CopyableOnly&) = default;  // allow copy-assign

  CopyableOnly(CopyableOnly&&) = delete;  // disallow move
  CopyableOnly& operator=(CopyableOnly&&) = delete;

  ~CopyableOnly() = default;

 protected:
  // optional helper for derived classes
  Derived& derived() noexcept { return static_cast<Derived&>(*this); }
  const Derived& derived() const noexcept {
    return static_cast<const Derived&>(*this);
  }
};

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
  Derived& derived() noexcept { return static_cast<Derived&>(*this); }
  const Derived& derived() const noexcept {
    return static_cast<const Derived&>(*this);
  }
};

template <typename Derived>
struct CopyableMovable {
  CopyableMovable() = default;

  CopyableMovable(const CopyableMovable&) = default;  // allow copy
  CopyableMovable& operator=(const CopyableMovable&) = default;

  CopyableMovable(CopyableMovable&&) noexcept = default;  // allow move
  CopyableMovable& operator=(CopyableMovable&&) noexcept = default;

  ~CopyableMovable() = default;

 protected:
  Derived& derived() noexcept { return static_cast<Derived&>(*this); }
  const Derived& derived() const noexcept {
    return static_cast<const Derived&>(*this);
  }
};

template <typename Derived>
struct NonCopyableNonMovable {
  NonCopyableNonMovable() = default;

  NonCopyableNonMovable(const NonCopyableNonMovable&) = delete;  // disable both
  NonCopyableNonMovable& operator=(const NonCopyableNonMovable&) = delete;

  NonCopyableNonMovable(NonCopyableNonMovable&&) = delete;
  NonCopyableNonMovable& operator=(NonCopyableNonMovable&&) = delete;

  ~NonCopyableNonMovable() = default;

 protected:
  Derived& derived() noexcept { return static_cast<Derived&>(*this); }
  const Derived& derived() const noexcept {
    return static_cast<const Derived&>(*this);
  }
};

}  // namespace bsrvcore

#endif
