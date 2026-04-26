/**
 * @file stream_builder.cc
 * @brief DirectStreamBuilder and PooledStreamBuilder implementations.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-26
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/client/stream_builder.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/system/errc.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "bsrvcore/connection/client/stream_slot.h"
#include "bsrvcore/core/types.h"

namespace bsrvcore {

// ---- DirectStreamBuilder ----

namespace {

/**
 * Call chain for DirectStreamBuilder::Acquire:
 *
 *   Acquire
 *     → resolver.async_resolve
 *       → OnDirectResolve (lambda)
 *         → DoDirectSslConnect  (if ssl_ctx != nullptr)
 *           → ssl_stream.async_connect
 *             → OnDirectSslConnect (lambda)
 *               → ssl_stream.async_handshake
 *                 → OnDirectSslHandshake (lambda) → cb(slot)
 *         → DoDirectTcpConnect  (otherwise)
 *           → tcp_stream.async_connect
 *             → OnDirectTcpConnect (lambda) → cb(slot)
 */

/** @brief Connect plain TCP and produce a StreamSlot. */
void DoDirectTcpConnect(ConnectionKey key, IoContextExecutor executor,
                        const TcpResolverResults& results,
                        DirectStreamBuilder::AcquireCallback cb) {
  auto tcp_stream = std::make_unique<TcpStream>(executor);
  tcp_stream->expires_after(std::chrono::seconds(2));

  tcp_stream->async_connect(
      results, [key = std::move(key), tcp_stream = std::move(tcp_stream),
                cb = std::move(cb)](boost::system::error_code ec,
                                    const TcpEndpoint&) mutable {
        if (ec) {
          cb(ec, StreamSlot{});
          return;
        }
        StreamSlot slot;
        slot.key = std::move(key);
        slot.tcp_stream = std::move(tcp_stream);
        slot.http_version = 11;
        cb({}, std::move(slot));
      });
}

/** @brief Connect SSL (TCP + SNI + handshake) and produce a StreamSlot. */
void DoDirectSslConnect(ConnectionKey key, IoContextExecutor executor,
                        const TcpResolverResults& results,
                        DirectStreamBuilder::AcquireCallback cb) {
  auto ssl_stream = std::make_unique<SslStream>(executor, *key.ssl_ctx);
  boost::beast::get_lowest_layer(*ssl_stream)
      .expires_after(std::chrono::seconds(2));

  boost::beast::get_lowest_layer(*ssl_stream)
      .async_connect(results, [key = std::move(key),
                               ssl_stream = std::move(ssl_stream),
                               cb = std::move(cb)](boost::system::error_code ec,
                                                   const TcpEndpoint&) mutable {
        if (ec) {
          cb(ec, StreamSlot{});
          return;
        }

        // SNI hostname.
        if (SSL_set_tlsext_host_name(ssl_stream->native_handle(),
                                     key.host.c_str()) != 1) {
          cb(boost::system::error_code{static_cast<int>(::ERR_get_error()),
                                       boost::asio::error::get_ssl_category()},
             StreamSlot{});
          return;
        }

        // Peer verification.
        if (key.verify_peer) {
          ssl_stream->set_verify_mode(boost::asio::ssl::verify_peer);
          ssl_stream->set_verify_callback(
              boost::asio::ssl::host_name_verification(key.host));
        } else {
          ssl_stream->set_verify_mode(boost::asio::ssl::verify_none);
        }

        boost::beast::get_lowest_layer(*ssl_stream)
            .expires_after(std::chrono::seconds(2));

        // Wrap in shared_ptr so it outlives the handshake lambda.
        auto ssl_ptr =
            std::make_shared<std::unique_ptr<SslStream>>(std::move(ssl_stream));
        (**ssl_ptr).async_handshake(
            boost::asio::ssl::stream_base::client,
            [key = std::move(key), ssl_ptr,
             cb = std::move(cb)](boost::system::error_code ec) mutable {
              if (ec) {
                cb(ec, StreamSlot{});
                return;
              }
              StreamSlot slot;
              slot.key = std::move(key);
              slot.ssl_stream = std::move(*ssl_ptr);
              slot.sni_hostname = slot.key.host;
              slot.http_version = 11;
              cb({}, std::move(slot));
            });
      });
}

}  // namespace

std::shared_ptr<DirectStreamBuilder> DirectStreamBuilder::Create() {
  void* raw =
      Allocate(sizeof(DirectStreamBuilder), alignof(DirectStreamBuilder));
  try {
    auto* builder = new (raw) DirectStreamBuilder();
    return {builder, [](DirectStreamBuilder* ptr) { DestroyDeallocate(ptr); }};
  } catch (...) {
    Deallocate(raw, sizeof(DirectStreamBuilder), alignof(DirectStreamBuilder));
    throw;
  }
}

void DirectStreamBuilder::Acquire(ConnectionKey key, IoContextExecutor executor,
                                  AcquireCallback cb) {
  // Share resolver across the async chain.
  auto resolver = std::make_shared<TcpResolver>(executor);

  // Copy host/port before the lambda capture moves `key`, since C++ argument
  // evaluation order is unspecified.
  const std::string resolve_host = key.host;
  const std::string resolve_port = key.port;

  resolver->async_resolve(
      resolve_host, resolve_port,
      [this_shared = shared_from_this(), key = std::move(key),
       executor = std::move(executor), cb = std::move(cb),
       resolver](boost::system::error_code ec,
                 const TcpResolverResults& results) mutable {
        if (ec) {
          cb(ec, StreamSlot{});
          return;
        }

        if (key.ssl_ctx != nullptr) {
          DoDirectSslConnect(std::move(key), std::move(executor), results,
                             std::move(cb));
        } else {
          DoDirectTcpConnect(std::move(key), std::move(executor), results,
                             std::move(cb));
        }
      });
}

void DirectStreamBuilder::Return(StreamSlot slot) {
  boost::system::error_code ignored;
  if (slot.ssl_stream) {
    auto& socket = boost::beast::get_lowest_layer(*slot.ssl_stream).socket();
    socket.cancel(ignored);
    socket.shutdown(Tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
  }
  if (slot.tcp_stream) {
    auto& socket = slot.tcp_stream->socket();
    socket.cancel(ignored);
    socket.shutdown(Tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
  }
}

// ---- PooledStreamBuilder ----

std::shared_ptr<PooledStreamBuilder> PooledStreamBuilder::Create(
    std::chrono::steady_clock::duration idle_timeout) {
  void* raw =
      Allocate(sizeof(PooledStreamBuilder), alignof(PooledStreamBuilder));
  try {
    auto* builder = new (raw) PooledStreamBuilder(idle_timeout);
    return {builder, [](PooledStreamBuilder* ptr) { DestroyDeallocate(ptr); }};
  } catch (...) {
    Deallocate(raw, sizeof(PooledStreamBuilder), alignof(PooledStreamBuilder));
    throw;
  }
}

PooledStreamBuilder::PooledStreamBuilder(
    std::chrono::steady_clock::duration idle_timeout)
    : idle_timeout_(idle_timeout) {}

void PooledStreamBuilder::Acquire(ConnectionKey key, IoContextExecutor executor,
                                  AcquireCallback cb) {
  {
    std::scoped_lock const lock(mutex_);
    auto it = pool_.find(key);
    if (it != pool_.end()) {
      auto& dq = it->second;
      while (!dq.empty()) {
        auto slot = std::move(dq.front());
        dq.pop_front();
        if (slot.IsCompatible(key) && slot.IsReusable()) {
          return cb({}, std::move(slot));
        }
        // Stale or incompatible — discard (destroyed on scope exit).
      }
      if (dq.empty()) {
        pool_.erase(it);
      }
    }
  }

  // Pool miss — create a fresh connection.
  auto direct = DirectStreamBuilder::Create();
  direct->Acquire(std::move(key), executor, std::move(cb));
}

void PooledStreamBuilder::Return(StreamSlot slot) {
  if (!slot.IsReusable()) {
    boost::system::error_code ignored;
    if (slot.ssl_stream) {
      auto& socket = boost::beast::get_lowest_layer(*slot.ssl_stream).socket();
      socket.cancel(ignored);
      socket.shutdown(Tcp::socket::shutdown_both, ignored);
      socket.close(ignored);
    }
    if (slot.tcp_stream) {
      auto& socket = slot.tcp_stream->socket();
      socket.cancel(ignored);
      socket.shutdown(Tcp::socket::shutdown_both, ignored);
      socket.close(ignored);
    }
    return;
  }

  slot.idle_deadline = std::chrono::steady_clock::now() + idle_timeout_;

  std::scoped_lock const lock(mutex_);
  pool_[slot.key].push_back(std::move(slot));
}

}  // namespace bsrvcore
