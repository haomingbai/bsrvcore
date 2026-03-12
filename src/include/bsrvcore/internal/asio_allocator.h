/**
 * @file asio_allocator.h
 * @brief Small per-connection allocator for Boost.Asio handler allocation.
 * @author Haoming Bai
 * @date   2026-03-12
 *
 * @details
 * Provides a lightweight memory pool and a standard-conforming Allocator that
 * can be used with `boost::asio::bind_allocator(...)`.
 *
 * Design goals:
 * - Per-connection: allocator instance is owned by a connection.
 * - Handler-friendly: very fast for small allocations; fallback to ::operator new.
 * - Thread-safe: tasks may execute on a thread_pool concurrently.
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_ASIO_ALLOCATOR_H_
#define BSRVCORE_INTERNAL_ASIO_ALLOCATOR_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace bsrvcore::internal {

class HandlerMemory {
 public:
  // Default tuned for typical Asio handler allocations.
  static constexpr std::size_t kDefaultBlockSize = 32 * 1024;
  static constexpr std::size_t kDefaultBlockCount = 4;

  explicit HandlerMemory(std::size_t block_size = kDefaultBlockSize,
                         std::size_t block_count = kDefaultBlockCount)
      : block_size_(block_size), blocks_() {
    blocks_.reserve(block_count);
    for (std::size_t i = 0; i < block_count; ++i) {
      blocks_.emplace_back(Block{std::unique_ptr<std::byte[]>(
                                     new (std::nothrow) std::byte[block_size]),
                                 false});
    }
  }

  void* Allocate(std::size_t bytes, std::size_t alignment) {
    // Alignment handling: our blocks are byte arrays; we only guarantee
    // max_align_t alignment by over-allocating via global new.
    // For simplicity and safety, if alignment exceeds alignof(max_align_t)
    // or size exceeds block size, fallback to global new.
    if (bytes == 0) {
      return nullptr;
    }

    if (alignment > alignof(std::max_align_t) || bytes > block_size_) {
      return ::operator new(bytes);
    }

    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& block : blocks_) {
      if (!block.data) {
        continue;
      }
      if (!block.in_use) {
        block.in_use = true;
        return block.data.get();
      }
    }

    return ::operator new(bytes);
  }

  void Deallocate(void* ptr, std::size_t /*bytes*/, std::size_t /*alignment*/) {
    if (ptr == nullptr) {
      return;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& block : blocks_) {
      if (block.data && block.data.get() == ptr) {
        block.in_use = false;
        return;
      }
    }

    ::operator delete(ptr);
  }

 private:
  struct Block {
    std::unique_ptr<std::byte[]> data;
    bool in_use;
  };

  std::size_t block_size_;
  std::mutex mtx_;
  std::vector<Block> blocks_;
};

template <typename T>
class TaskAllocator {
 public:
  using value_type = T;

  TaskAllocator() noexcept = default;

  explicit TaskAllocator(std::shared_ptr<HandlerMemory> mem) noexcept
      : mem_(std::move(mem)) {}

  template <typename U>
  TaskAllocator(const TaskAllocator<U>& other) noexcept : mem_(other.memory()) {}

  [[nodiscard]] T* allocate(std::size_t n) {
    if (n == 0) {
      return nullptr;
    }

    if (!mem_) {
      return std::allocator<T>{}.allocate(n);
    }

    void* p = mem_->Allocate(sizeof(T) * n, alignof(T));
    return static_cast<T*>(p);
  }

  void deallocate(T* p, std::size_t n) noexcept {
    if (!mem_) {
      std::allocator<T>{}.deallocate(p, n);
      return;
    }

    mem_->Deallocate(p, sizeof(T) * n, alignof(T));
  }

  template <typename U>
  struct rebind {
    using other = TaskAllocator<U>;
  };

  using is_always_equal = std::false_type;

  std::shared_ptr<HandlerMemory> memory() const noexcept { return mem_; }

  friend bool operator==(const TaskAllocator& a, const TaskAllocator& b) noexcept {
    return a.mem_ == b.mem_;
  }

 private:
  std::shared_ptr<HandlerMemory> mem_;
};

using HandlerAllocator = TaskAllocator<std::byte>;

}  // namespace bsrvcore::internal

#endif  // BSRVCORE_INTERNAL_ASIO_ALLOCATOR_H_
