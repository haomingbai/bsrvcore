/**
 * @file heap.h
 * @brief Internal binary heap container.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-25
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides a 1-indexed binary heap used internally for scheduling.
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_SESSION_HEAP_H_
#define BSRVCORE_INTERNAL_SESSION_HEAP_H_

#include <cassert>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "bsrvcore/core/trait.h"

namespace bsrvcore {

template <typename T, typename C = std::less<T>>
/**
 * @brief A simple 1-indexed binary heap.
 *
 * @tparam T Value type.
 * @tparam C Comparator. The heap property is defined by `C`.
 *
 * @note This is an internal utility. The container keeps an unused sentinel
 *       element at index 0, so the first valid element is at index 1.
 */
class Heap : CopyableMovable<Heap<T>> {
 public:
  /**
   * @brief Push a new element into the heap.
   * @tparam Args Constructor argument types forwarded to T.
   * @param val Arguments to construct the element.
   * @return true on success.
   */
  template <typename... Args>
  bool Push(Args&&... val) {
    auto curr_idx = container_.size();
    container_.emplace_back(std::forward<Args>(val)...);
    T tmp = std::move(container_.back());

    // Modify the heap.
    {
      auto child = curr_idx;
      auto parent = child / 2;

      while (parent) {
        if (comp_(container_[parent], tmp)) {
          container_[child] = std::move(container_[parent]);
          child = parent;
          parent /= 2;
        } else {
          break;
        }
      }

      container_[child] = std::move(tmp);
    }

    return true;
  }

  /**
   * @brief Remove and return the top element.
   * @return The previous top element.
   *
   * @note Precondition: heap is not empty.
   */
  T Pop() noexcept {
    constexpr std::size_t root = 1;
    auto last_idx = container_.size() - 1;

    auto result = std::move(container_[root]);
    auto tmp = std::move(container_[last_idx]);
    container_.pop_back();

    {
      last_idx--;
      if (last_idx < 1) {
        return result;
      }

      auto curr = root;

      while (curr * 2 <= last_idx) {
        auto left = curr * 2;
        auto right = left + 1;

        [[unlikely]] if (right > last_idx) {
          if (comp_(tmp, container_[left])) {
            container_[curr] = std::move(container_[left]);
            curr = left;
          } else {
            break;
          }
        } else {
          assert(right <= last_idx);
          auto to_comp =
              comp_(container_[left], container_[right]) ? right : left;

          if (comp_(tmp, container_[to_comp])) {
            container_[curr] = std::move(container_[to_comp]);
            curr = to_comp;
          } else {
            break;
          }
        }
      }

      container_[curr] = std::move(tmp);
    }

    return result;
  }

  /**
   * @brief Read the current top element.
   * @return Reference to the top element.
   *
   * @note Precondition: heap is not empty.
   */
  [[nodiscard]] const T& Top() const noexcept {
    assert(!IsEmpty());
    return container_[1];
  }

  /**
   * @brief Shrink internal storage capacity to fit size.
   * @return true on success.
   */
  bool ShrinkToFit() {
    container_.shrink_to_fit();
    return true;
  }

  /**
   * @brief Get number of elements in the heap.
   * @return Element count.
   */
  [[nodiscard]] std::size_t GetSize() const noexcept {
    assert(!container_.empty());
    return container_.size() - 1;
  }

  /**
   * @brief Get current capacity (in elements) of the heap.
   * @return Capacity.
   */
  [[nodiscard]] std::size_t GetCapacity() const noexcept {
    assert(container_.capacity());
    return container_.capacity() - 1;
  }

  /**
   * @brief Whether the heap has no elements.
   * @return true if empty.
   */
  [[nodiscard]] bool IsEmpty() const noexcept {
    assert(!container_.empty());
    return (container_.size() <= 1);
  }

  /**
   * @brief Reserve space for at least n elements.
   * @param n Number of elements to reserve.
   * @return true on success.
   */
  bool Reserve(std::size_t n) {
    container_.reserve(n + 1);
    return true;
  }

  /**
   * @brief Construct an empty heap.
   */
  Heap() : container_(1) {}

  template <typename... Comp>
  Heap() : container_(1) {}

 private:
  std::vector<T> container_;
  C comp_;
};
}  // namespace bsrvcore

#endif
