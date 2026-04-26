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
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/asio/write.hpp>
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
    std::shared_ptr<StreamBuilder> inner,
    std::chrono::steady_clock::duration idle_timeout) {
  void* raw =
      Allocate(sizeof(PooledStreamBuilder), alignof(PooledStreamBuilder));
  try {
    auto* builder =
        new (raw) PooledStreamBuilder(std::move(inner), idle_timeout);
    return {builder, [](PooledStreamBuilder* ptr) { DestroyDeallocate(ptr); }};
  } catch (...) {
    Deallocate(raw, sizeof(PooledStreamBuilder), alignof(PooledStreamBuilder));
    throw;
  }
}

PooledStreamBuilder::PooledStreamBuilder(
    std::shared_ptr<StreamBuilder> inner,
    std::chrono::steady_clock::duration idle_timeout)
    : StreamBuilderDecorator(std::move(inner)), idle_timeout_(idle_timeout) {}

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

  // Pool miss — delegate to inner builder for a fresh connection.
  inner_->Acquire(std::move(key), executor, std::move(cb));
}

void PooledStreamBuilder::Return(StreamSlot slot) {
  if (!slot.IsReusable()) {
    // Delegate to inner builder (which closes the connection).
    inner_->Return(std::move(slot));
    return;
  }

  slot.idle_deadline = std::chrono::steady_clock::now() + idle_timeout_;

  std::scoped_lock const lock(mutex_);
  pool_[slot.key].push_back(std::move(slot));
}

// ---- StreamBuilderDecorator ----

StreamBuilderDecorator::StreamBuilderDecorator(
    std::shared_ptr<StreamBuilder> inner)
    : inner_(std::move(inner)) {}

void StreamBuilderDecorator::Acquire(ConnectionKey key,
                                     IoContextExecutor executor,
                                     AcquireCallback cb) {
  inner_->Acquire(std::move(key), std::move(executor), std::move(cb));
}

void StreamBuilderDecorator::Return(StreamSlot slot) {
  inner_->Return(std::move(slot));
}

// ---- WebSocketStreamBuilder ----

namespace {

/**
 * Call chain for WebSocketStreamBuilder::Acquire (WSS path):
 *
 *   Acquire
 *     → resolver.async_resolve
 *       → OnWebSocketResolve (lambda)
 *         → DoWebSocketTcpConnect
 *           → tcp_stream.async_connect
 *             → OnWebSocketTcpConnect (lambda) → cb(slot)
 */

/** @brief Connect plain TCP for deferred WSS and produce a StreamSlot. */
void DoWebSocketTcpConnect(ConnectionKey key, IoContextExecutor executor,
                           const TcpResolverResults& results,
                           StreamBuilder::AcquireCallback cb) {
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

        // Return TcpStream with deferred SSL info.
        // WebSocketClientTask will wrap this in websocket::stream<SslStream>
        // and perform the TLS handshake itself.
        StreamSlot slot;
        slot.key = key;
        slot.tcp_stream = std::move(tcp_stream);
        slot.deferred_ssl_ctx = key.ssl_ctx;
        slot.deferred_verify_peer = key.verify_peer;
        slot.sni_hostname = key.host;
        slot.http_version = 11;
        cb({}, std::move(slot));
      });
}

}  // namespace

std::shared_ptr<WebSocketStreamBuilder> WebSocketStreamBuilder::Create(
    std::shared_ptr<StreamBuilder> inner, SslContextPtr ssl_ctx,
    bool verify_peer) {
  void* raw =
      Allocate(sizeof(WebSocketStreamBuilder), alignof(WebSocketStreamBuilder));
  try {
    auto* builder = new (raw) WebSocketStreamBuilder(
        std::move(inner), std::move(ssl_ctx), verify_peer);
    return {builder,
            [](WebSocketStreamBuilder* ptr) { DestroyDeallocate(ptr); }};
  } catch (...) {
    Deallocate(raw, sizeof(WebSocketStreamBuilder),
               alignof(WebSocketStreamBuilder));
    throw;
  }
}

WebSocketStreamBuilder::WebSocketStreamBuilder(
    std::shared_ptr<StreamBuilder> inner, SslContextPtr ssl_ctx,
    bool verify_peer)
    : StreamBuilderDecorator(std::move(inner)),
      ssl_ctx_(std::move(ssl_ctx)),
      verify_peer_(verify_peer) {}

void WebSocketStreamBuilder::Acquire(ConnectionKey key,
                                     IoContextExecutor executor,
                                     AcquireCallback cb) {
  // For non-SSL WebSocket, forward to inner builder (plain TCP).
  if (key.ssl_ctx == nullptr) {
    inner_->Acquire(std::move(key), std::move(executor), std::move(cb));
    return;
  }

  // For WSS: resolve DNS + connect TCP only, defer TLS handshake.
  // Beast's websocket::stream<SslStream> expects handshake after wrapping.
  auto resolver = std::make_shared<TcpResolver>(executor);

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
        DoWebSocketTcpConnect(std::move(key), std::move(executor), results,
                              std::move(cb));
      });
}

// ---- ProxyStreamBuilder ----

namespace {

/**
 * Call chain for ProxyStreamBuilder::Acquire (HTTPS proxy path):
 *
 *   Acquire
 *     → inner_.Acquire (connect to proxy via TCP)
 *       → OnProxyTcpAcquired
 *         → DoProxyConnect (send CONNECT request)
 *           → async_write → OnProxyConnectWritten
 *             → async_read → OnProxyConnectResponse
 *               → DoProxyTlsHandshake
 *                 → ssl_stream.async_handshake → OnProxyTlsHandshake
 *                   → cb(slot)
 */

// Forward declaration: DoProxyTlsHandshake is called from DoProxyConnect's
// lambda but defined after it.
void DoProxyTlsHandshake(ConnectionKey original_key,
                         std::shared_ptr<TcpStream> tcp_stream,
                         StreamBuilder::AcquireCallback cb);

/** @brief Send CONNECT request and read response on the proxy TCP stream. */
void DoProxyConnect(ConnectionKey original_key,
                    std::shared_ptr<TcpStream> tcp_stream,
                    StreamBuilder::AcquireCallback cb) {
  // Build CONNECT request.
  const std::string connect_target =
      original_key.host + ":" + original_key.port;
  std::string connect_req = "CONNECT " + connect_target + " HTTP/1.1\r\n" +
                            "Host: " + connect_target + "\r\n" +
                            "Proxy-Connection: Keep-Alive\r\n";
  if (!original_key.proxy_ssl_ctx) {
    // Should not happen, but guard against it.
    cb(make_error_code(boost::system::errc::invalid_argument), StreamSlot{});
    return;
  }
  connect_req += "\r\n";

  auto buffer = std::make_shared<std::string>();
  tcp_stream->expires_after(std::chrono::seconds(5));

  boost::asio::async_write(
      *tcp_stream, boost::asio::buffer(connect_req),
      [tcp_stream, buffer, original_key = std::move(original_key),
       cb = std::move(cb)](boost::system::error_code ec, std::size_t) mutable {
        if (ec) {
          cb(ec, StreamSlot{});
          return;
        }

        // Read CONNECT response.
        tcp_stream->expires_after(std::chrono::seconds(5));
        boost::asio::async_read_until(
            *tcp_stream, boost::asio::dynamic_buffer(*buffer), "\r\n\r\n",
            [tcp_stream, buffer, original_key = std::move(original_key),
             cb = std::move(cb)](boost::system::error_code ec,
                                 std::size_t) mutable {
              if (ec) {
                cb(ec, StreamSlot{});
                return;
              }

              // Parse status code from "HTTP/1.x STATUS ..."
              const std::string& resp = *buffer;
              const auto space1 = resp.find(' ');
              if (space1 == std::string::npos || space1 + 1 >= resp.size()) {
                cb(make_error_code(boost::system::errc::protocol_error),
                   StreamSlot{});
                return;
              }
              const auto space2 = resp.find(' ', space1 + 1);
              const std::string status_str =
                  resp.substr(space1 + 1, space2 == std::string::npos
                                              ? std::string::npos
                                              : space2 - space1 - 1);
              const int status = std::stoi(status_str);
              if (status != 200) {
                cb(make_error_code(boost::system::errc::connection_refused),
                   StreamSlot{});
                return;
              }

              // CONNECT succeeded — perform TLS handshake on the tunnel.
              DoProxyTlsHandshake(std::move(original_key),
                                  std::move(tcp_stream), std::move(cb));
            });
      });
}

/** @brief Perform TLS handshake on the CONNECT tunnel. */
void DoProxyTlsHandshake(ConnectionKey original_key,
                         std::shared_ptr<TcpStream> tcp_stream,
                         StreamBuilder::AcquireCallback cb) {
  auto ssl_stream = std::make_shared<SslStream>(std::move(*tcp_stream),
                                                *original_key.proxy_ssl_ctx);

  // SNI hostname = original target host.
  if (SSL_set_tlsext_host_name(ssl_stream->native_handle(),
                               original_key.host.c_str()) != 1) {
    cb(boost::system::error_code{static_cast<int>(::ERR_get_error()),
                                 boost::asio::error::get_ssl_category()},
       StreamSlot{});
    return;
  }

  // Peer verification.
  if (original_key.verify_peer) {
    ssl_stream->set_verify_mode(boost::asio::ssl::verify_peer);
    ssl_stream->set_verify_callback(
        boost::asio::ssl::host_name_verification(original_key.host));
  } else {
    ssl_stream->set_verify_mode(boost::asio::ssl::verify_none);
  }

  boost::beast::get_lowest_layer(*ssl_stream)
      .expires_after(std::chrono::seconds(5));

  (*ssl_stream)
      .async_handshake(
          boost::asio::ssl::stream_base::client,
          [original_key = std::move(original_key), ssl_stream,
           cb = std::move(cb)](boost::system::error_code ec) mutable {
            if (ec) {
              cb(ec, StreamSlot{});
              return;
            }

            StreamSlot slot;
            slot.key = original_key;
            slot.ssl_stream =
                std::make_unique<SslStream>(std::move(*ssl_stream));
            slot.sni_hostname = original_key.host;
            slot.http_version = 11;
            cb({}, std::move(slot));
          });
}

/** @brief Handle TCP connection to proxy server. */
void OnProxyTcpAcquired(ConnectionKey original_key,
                        StreamBuilder::AcquireCallback cb,
                        boost::system::error_code ec, StreamSlot slot) {
  if (ec) {
    cb(ec, StreamSlot{});
    return;
  }

  if (!slot.tcp_stream) {
    cb(make_error_code(boost::system::errc::invalid_argument), StreamSlot{});
    return;
  }

  auto tcp_ptr = std::make_shared<TcpStream>(std::move(*slot.tcp_stream));
  DoProxyConnect(std::move(original_key), std::move(tcp_ptr), std::move(cb));
}

}  // namespace

std::shared_ptr<ProxyStreamBuilder> ProxyStreamBuilder::Create(
    std::shared_ptr<StreamBuilder> inner) {
  void* raw = Allocate(sizeof(ProxyStreamBuilder), alignof(ProxyStreamBuilder));
  try {
    auto* builder = new (raw) ProxyStreamBuilder(std::move(inner));
    return {builder, [](ProxyStreamBuilder* ptr) { DestroyDeallocate(ptr); }};
  } catch (...) {
    Deallocate(raw, sizeof(ProxyStreamBuilder), alignof(ProxyStreamBuilder));
    throw;
  }
}

ProxyStreamBuilder::ProxyStreamBuilder(std::shared_ptr<StreamBuilder> inner)
    : StreamBuilderDecorator(std::move(inner)) {}

void ProxyStreamBuilder::Acquire(ConnectionKey key, IoContextExecutor executor,
                                 AcquireCallback cb) {
  // Non-proxy or HTTP proxy: delegate to inner builder.
  // HTTP proxy only needs request target rewriting (done by
  // ProxyRequestAssembler); the TCP connection goes directly to the proxy.
  if (!key.has_proxy() || key.scheme != "https") {
    inner_->Acquire(std::move(key), std::move(executor), std::move(cb));
    return;
  }

  // HTTPS proxy: connect to proxy via TCP, then CONNECT tunnel, then TLS.
  // Save original target info before modifying the key for proxy connection.
  ConnectionKey original_key = key;

  // Create a key that points to the proxy server (plain TCP, no SSL).
  ConnectionKey proxy_key;
  proxy_key.scheme = "http";  // Plain TCP to proxy.
  proxy_key.host = key.proxy_host;
  proxy_key.port = key.proxy_port;
  proxy_key.ssl_ctx = nullptr;
  proxy_key.verify_peer = false;  // No TLS to proxy.

  inner_->Acquire(std::move(proxy_key), std::move(executor),
                  [original_key = std::move(original_key), cb = std::move(cb)](
                      boost::system::error_code ec, StreamSlot slot) mutable {
                    OnProxyTcpAcquired(std::move(original_key), std::move(cb),
                                       ec, std::move(slot));
                  });
}

}  // namespace bsrvcore
