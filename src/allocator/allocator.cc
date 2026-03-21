/**
 * @file allocator.cc
 * @brief mimalloc-backed public allocation ABI for bsrvcore.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-21
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/allocator.h"

#include <mimalloc.h>

#include <cassert>
#include <new>

namespace bsrvcore {

namespace {

constexpr bool IsPowerOfTwo(std::size_t value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace

void* AllocatorMemoryResource::do_allocate(std::size_t bytes,
                                           std::size_t alignment) {
  return Allocate(bytes, alignment);
}

void AllocatorMemoryResource::do_deallocate(void* p, std::size_t bytes,
                                            std::size_t alignment) {
  Deallocate(p, bytes, alignment);
}

bool AllocatorMemoryResource::do_is_equal(
    const std::pmr::memory_resource& other) const noexcept {
  return this == &other;
}

std::pmr::memory_resource* GetDefaultMemoryResource() noexcept {
  static AllocatorMemoryResource resource{};
  return &resource;
}

void* Allocate(std::size_t size, std::size_t alignment) {
  if (size == 0) {
    return nullptr;
  }

  if (alignment < alignof(void*)) {
    alignment = alignof(void*);
  }

  if (!IsPowerOfTwo(alignment)) {
    throw std::bad_alloc();
  }

  void* ptr = nullptr;
  if (alignment <= alignof(std::max_align_t)) {
    ptr = mi_malloc(size);
  } else {
    ptr = mi_malloc_aligned(size, alignment);
  }

  if (ptr == nullptr) {
    throw std::bad_alloc();
  }

  return ptr;
}

void Deallocate(void* ptr, std::size_t /*size*/,
                std::size_t alignment) noexcept {
  if (ptr == nullptr) {
    return;
  }

  if (alignment < alignof(void*)) {
    alignment = alignof(void*);
  }

  if (!IsPowerOfTwo(alignment)) {
    assert(false && "Deallocate called with non power-of-two alignment");
    mi_free(ptr);
    return;
  }

  if (alignment <= alignof(std::max_align_t)) {
    mi_free(ptr);
  } else {
    mi_free_aligned(ptr, alignment);
  }
}

}  // namespace bsrvcore
