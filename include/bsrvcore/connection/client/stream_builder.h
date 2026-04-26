/**
 * @file stream_builder.h
 * @brief Connection acquisition/return abstraction for HTTP client.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-26
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_STREAM_BUILDER_H_
#define BSRVCORE_CONNECTION_CLIENT_STREAM_BUILDER_H_

#include <boost/system/error_code.hpp>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/stream_slot.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"

namespace bsrvcore {

/**
 * @brief Abstract interface for acquiring and returning reusable connections.
 *
 * Concrete implementations decide how connections are created, cached,
 * and released.
 */
class StreamBuilder : public std::enable_shared_from_this<StreamBuilder>,
                      public NonCopyableNonMovable<StreamBuilder> {
 public:
  /** @brief Callback type for async Acquire completion. */
  using AcquireCallback =
      std::function<void(boost::system::error_code, StreamSlot)>;

  ~StreamBuilder() = default;

  /**
   * @brief Asynchronously acquire a stream matching the given key.
   *
   * Implementations may either return a cached idle stream or create a
   * fresh connection (DNS → TCP → optional TLS).
   *
   * @param key Connection identity for selection/compatibility check.
   * @param executor io_context executor driving async I/O.
   * @param cb Completion callback (always on strand, never null).
   */
  virtual void Acquire(ConnectionKey key, IoContextExecutor executor,
                       AcquireCallback cb) = 0;

  /**
   * @brief Return a previously acquired stream.
   *
   * Implementations decide whether to cache or close the stream.
   */
  virtual void Return(StreamSlot slot) = 0;
};

/**
 * @brief One-shot connection builder: creates a fresh connection per Acquire.
 *
 * Return() immediately closes the transport.
 */
class DirectStreamBuilder : public StreamBuilder {
 public:
  /** @brief Create a DirectStreamBuilder. */
  static std::shared_ptr<DirectStreamBuilder> Create();

  void Acquire(ConnectionKey key, IoContextExecutor executor,
               AcquireCallback cb) override;
  void Return(StreamSlot slot) override;

 private:
  DirectStreamBuilder() = default;
};

/**
 * @brief Connection-pooled builder: caches idle StreamSlots keyed by
 * ConnectionKey.
 *
 * Acquire() first scans the pool for a compatible reusable slot; on miss
 * it falls back to creating a new connection. Return() inserts the slot
 * back into the pool if reusable, otherwise closes it.
 */
class PooledStreamBuilder : public StreamBuilder {
 public:
  /** @brief Create a PooledStreamBuilder with an optional per-slot idle
   * duration. */
  static std::shared_ptr<PooledStreamBuilder> Create(
      std::chrono::steady_clock::duration idle_timeout =
          std::chrono::seconds(60));

  void Acquire(ConnectionKey key, IoContextExecutor executor,
               AcquireCallback cb) override;
  void Return(StreamSlot slot) override;

 private:
  explicit PooledStreamBuilder(
      std::chrono::steady_clock::duration idle_timeout);

  std::chrono::steady_clock::duration idle_timeout_;
  std::mutex mutex_;
  using PoolDeque = AllocatedDeque<StreamSlot>;
  using PoolMap =
      AllocatedUnorderedMap<ConnectionKey, PoolDeque, ConnectionKeyHash>;
  PoolMap pool_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_STREAM_BUILDER_H_
