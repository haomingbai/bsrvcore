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

  virtual ~StreamBuilder() = default;

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
 * @brief Base class for StreamBuilder decorators.
 *
 * Subclasses wrap an inner StreamBuilder and can intercept/modify
 * Acquire() and Return() calls. This enables composable connection
 * strategies like pooling, proxying, or WebSocket-specific TLS handling.
 *
 * Default behavior: forward all calls to inner_.
 */
class StreamBuilderDecorator : public StreamBuilder {
 public:
  void Acquire(ConnectionKey key, IoContextExecutor executor,
               AcquireCallback cb) override;
  void Return(StreamSlot slot) override;

 protected:
  explicit StreamBuilderDecorator(std::shared_ptr<StreamBuilder> inner);

  std::shared_ptr<StreamBuilder> inner_;
};

/**
 * @brief Connection-pooled builder: caches idle StreamSlots keyed by
 * ConnectionKey.
 *
 * Wraps an inner StreamBuilder (typically DirectStreamBuilder) for
 * creating new connections on pool misses. Acquire() first scans the
 * pool for a compatible reusable slot; on miss it delegates to inner_.
 * Return() inserts the slot back into the pool if reusable, otherwise
 * delegates to inner_ (which closes the connection).
 */
class PooledStreamBuilder : public StreamBuilderDecorator {
 public:
  /**
   * @brief Create a PooledStreamBuilder wrapping the given inner builder.
   *
   * @param inner Inner builder used for creating new connections on pool miss.
   * @param idle_timeout Maximum idle duration before a pooled slot is
   * discarded.
   */
  static std::shared_ptr<PooledStreamBuilder> Create(
      std::shared_ptr<StreamBuilder> inner,
      std::chrono::steady_clock::duration idle_timeout =
          std::chrono::seconds(60));

  void Acquire(ConnectionKey key, IoContextExecutor executor,
               AcquireCallback cb) override;
  void Return(StreamSlot slot) override;

 private:
  PooledStreamBuilder(std::shared_ptr<StreamBuilder> inner,
                      std::chrono::steady_clock::duration idle_timeout);

  std::chrono::steady_clock::duration idle_timeout_;
  std::mutex mutex_;
  using PoolDeque = AllocatedDeque<StreamSlot>;
  using PoolMap =
      AllocatedUnorderedMap<ConnectionKey, PoolDeque, ConnectionKeyHash>;
  PoolMap pool_;
};

/**
 * @brief WebSocket-specific builder that defers TLS handshake for WSS.
 *
 * DirectStreamBuilder completes TLS before returning an SslStream. WSS needs
 * different ownership because the WebSocket client task must build the
 * WebSocket stream around the TLS transport after it has prepared WebSocket
 * handshake state. This builder therefore stops at a connected TcpStream and
 * returns the TLS context as deferred metadata.
 *
 * WebSocketStreamBuilder solves this by:
 * - For WS (non-SSL): forwarding to the inner builder (plain TCP).
 * - For WSS (SSL): resolving DNS + connecting TCP, but NOT performing the
 *   TLS handshake. Returns a TcpStream with deferred_ssl_ctx and
 *   deferred_verify_peer set in the StreamSlot. WebSocketClientTask performs
 *   TLS using a temporary SslStream and then promotes it into the final
 *   websocket::stream<SslStream>.
 */
class WebSocketStreamBuilder : public StreamBuilderDecorator {
 public:
  /**
   * @brief Create a WebSocketStreamBuilder.
   *
   * @param inner Inner builder for WS (non-SSL) connections.
   * @param ssl_ctx SSL context for WSS connections.
   */
  static std::shared_ptr<WebSocketStreamBuilder> Create(
      std::shared_ptr<StreamBuilder> inner, SslContextPtr ssl_ctx);

  void Acquire(ConnectionKey key, IoContextExecutor executor,
               AcquireCallback cb) override;

 private:
  WebSocketStreamBuilder(std::shared_ptr<StreamBuilder> inner,
                         SslContextPtr ssl_ctx);

  SslContextPtr ssl_ctx_;
};

/**
 * @brief HTTPS proxy builder: establishes CONNECT tunnel + TLS.
 *
 * For HTTPS connections through a proxy, the flow is:
 *   1. Connect to proxy via plain TCP (inner builder, ssl_ctx=nullptr)
 *   2. Send CONNECT target_host:target_port
 *   3. Read CONNECT response (expect 200 Connection Established)
 *   4. Perform TLS handshake on the tunnel (SNI = target_host)
 *   5. Return the handshaked SslStream
 *
 * For HTTP proxy or non-proxy connections, delegates to inner builder.
 *
 * The proxy host/port are taken from ConnectionKey.proxy_host/proxy_port
 * (set by ProxyRequestAssembler). The actual target host/port are in
 * ConnectionKey.host/port.
 */
class ProxyStreamBuilder : public StreamBuilderDecorator {
 public:
  /**
   * @brief Create a ProxyStreamBuilder.
   *
   * @param inner Inner builder for non-proxy and HTTP proxy connections.
   */
  static std::shared_ptr<ProxyStreamBuilder> Create(
      std::shared_ptr<StreamBuilder> inner);

  void Acquire(ConnectionKey key, IoContextExecutor executor,
               AcquireCallback cb) override;

 private:
  explicit ProxyStreamBuilder(std::shared_ptr<StreamBuilder> inner);
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_STREAM_BUILDER_H_
