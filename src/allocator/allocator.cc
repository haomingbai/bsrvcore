/**
 * @file allocator.cc
 * @brief Platform-aware public allocation ABI for bsrvcore.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-21
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/allocator/allocator.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(BSRVCORE_USE_MIMALLOC)
#include <mimalloc.h>
#endif

namespace bsrvcore {

namespace {

constexpr bool IsPowerOfTwo(std::size_t value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}

#ifdef _WIN32
struct ProcessHeapAlignedHeader {
  void* base;
};

void* AllocateFromProcessHeap(std::size_t size, std::size_t alignment) {
  HANDLE heap = GetProcessHeap();
  if (heap == nullptr) {
    throw std::bad_alloc();
  }

  if (alignment <= MEMORY_ALLOCATION_ALIGNMENT) {
    void* ptr = HeapAlloc(heap, 0, size);
    if (ptr == nullptr) {
      throw std::bad_alloc();
    }
    return ptr;
  }

  const std::size_t extra = sizeof(ProcessHeapAlignedHeader) + alignment - 1;
  if (size > static_cast<std::size_t>(-1) - extra) {
    throw std::bad_alloc();
  }

  void* base = HeapAlloc(heap, 0, size + extra);
  if (base == nullptr) {
    throw std::bad_alloc();
  }

  const auto raw =
      reinterpret_cast<std::uintptr_t>(base) + sizeof(ProcessHeapAlignedHeader);
  const auto aligned = (raw + alignment - 1) & ~(alignment - 1);
  auto* header = reinterpret_cast<ProcessHeapAlignedHeader*>(
      aligned - sizeof(ProcessHeapAlignedHeader));
  header->base = base;
  return reinterpret_cast<void*>(aligned);
}

void DeallocateToProcessHeap(void* ptr, std::size_t alignment) noexcept {
  HANDLE heap = GetProcessHeap();
  if (heap == nullptr) {
    assert(false && "GetProcessHeap() returned nullptr during deallocation");
    return;
  }

  if (alignment <= MEMORY_ALLOCATION_ALIGNMENT) {
    HeapFree(heap, 0, ptr);
    return;
  }

  auto* header = reinterpret_cast<ProcessHeapAlignedHeader*>(
      reinterpret_cast<std::uintptr_t>(ptr) - sizeof(ProcessHeapAlignedHeader));
  HeapFree(heap, 0, header->base);
}
#endif

}  // namespace

void* Allocate(std::size_t size, std::size_t alignment) {
  if (size == 0) {
    return nullptr;
  }

  alignment = std::max(alignment, alignof(void*));

  if (!IsPowerOfTwo(alignment)) {
    throw std::bad_alloc();
  }

#ifdef _WIN32
  // bsrvrun plugins often cross DLL boundaries while Windows CI defaults to
  // static bsrvcore builds. Using the process heap keeps Allocate/Deallocate
  // ABI-safe even when the caller and callee live in different modules.
  return AllocateFromProcessHeap(size, alignment);
#elif defined(BSRVCORE_USE_MIMALLOC)
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
#else
  if (alignment <= alignof(std::max_align_t)) {
    if (void* ptr = std::malloc(size)) {
      return ptr;
    }
    throw std::bad_alloc();
  }

  if (size > std::numeric_limits<std::size_t>::max() - alignment) {
    throw std::bad_alloc();
  }
  // C11 aligned_alloc requires the size to be a multiple of the alignment.
  const std::size_t aligned_size =
      (size + alignment - 1) / alignment * alignment;
  if (void* ptr = std::aligned_alloc(alignment, aligned_size)) {
    return ptr;
  }
  throw std::bad_alloc();
#endif
}

void Deallocate(void* ptr, std::size_t /*size*/,
                std::size_t alignment) noexcept {
  if (ptr == nullptr) {
    return;
  }

  alignment = std::max(alignment, alignof(void*));

  if (!IsPowerOfTwo(alignment)) {
    assert(false && "Deallocate called with non power-of-two alignment");
#ifdef _WIN32
    return;
#else
    free(ptr);
    return;
#endif
  }

#ifdef _WIN32
  DeallocateToProcessHeap(ptr, alignment);
#elif defined(BSRVCORE_USE_MIMALLOC)
  if (alignment <= alignof(std::max_align_t)) {
    mi_free(ptr);
  } else {
    mi_free_aligned(ptr, alignment);
  }
#else
  free(ptr);
#endif
}

}  // namespace bsrvcore
