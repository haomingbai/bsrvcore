/**
 * @file heap.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-25
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_HEAP_H_
#define BSRVCORE_INTERNAL_HEAP_H_

#include <cassert>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "bsrvcore/trait.h"

namespace bsrvcore {

template <typename T, typename C = std::less<T>>
class Heap : CopyableMovable<Heap<T>> {
 public:
  template <typename... Args>
  bool Push(Args &&...val) {
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

  const T &Top() const noexcept {
    assert(!IsEmpty());
    return container_[1];
  }

  bool ShrinkToFit() {
    container_.shrink_to_fit();
    return true;
  }

  std::size_t GetSize() const noexcept {
    assert(container_.size());
    return container_.size() - 1;
  }

  std::size_t GetCapacity() const noexcept {
    assert(container_.capacity());
    return container_.capacity() - 1;
  }

  bool IsEmpty() const noexcept {
    assert(container_.size());
    return (container_.size() <= 1);
  }

  bool Reserve(std::size_t n) {
    container_.reserve(n + 1);
    return true;
  }

  Heap() : container_(1), comp_(C()) {}

  template <typename... Comp>
  Heap(Comp &&...c) : container_(1), comp_(std::forward<Comp>(c)...) {}

 private:
  std::vector<T> container_;
  C comp_;
};
}  // namespace bsrvcore

#endif
