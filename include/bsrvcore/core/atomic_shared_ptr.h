/**
 * @file atomic_shared_ptr.h
 * @brief Mutex-backed shared_ptr wrapper with atomic-style load/store API.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-03
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CORE_ATOMIC_SHARED_PTR_H_
#define BSRVCORE_CORE_ATOMIC_SHARED_PTR_H_

#include <atomic>
#include <memory>
#include <mutex>

namespace bsrvcore {

template <typename T>
class AtomicSharedPtr {
 public:
  AtomicSharedPtr() = default;
  explicit AtomicSharedPtr(std::shared_ptr<T> initial) : ptr_(std::move(initial)) {}

  AtomicSharedPtr(const AtomicSharedPtr&) = delete;
  AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

  std::shared_ptr<T> load(
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    // The wrapper keeps load/store call sites compatible with
    // std::atomic<std::shared_ptr<T>> while using portable mutex semantics.
    (void)order;
    std::lock_guard<std::mutex> lock(mtx_);
    return ptr_;
  }

  void store(
      std::shared_ptr<T> desired,
      std::memory_order order = std::memory_order_seq_cst) noexcept {
    (void)order;
    std::lock_guard<std::mutex> lock(mtx_);
    ptr_ = std::move(desired);
  }

 private:
  mutable std::mutex mtx_;
  std::shared_ptr<T> ptr_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CORE_ATOMIC_SHARED_PTR_H_
