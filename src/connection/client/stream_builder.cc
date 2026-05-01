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
#include <charconv>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
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

struct DirectResolveState {
  DirectResolveState(ConnectionKey key_in, IoContextExecutor executor_in,
                     DirectStreamBuilder::AcquireCallback cb_in)
      : key(std::move(key_in)),
        executor(std::move(executor_in)),
        resolver(executor),
        cb(std::move(cb_in)) {}

  ConnectionKey key;
  IoContextExecutor executor;
  TcpResolver resolver;
  DirectStreamBuilder::AcquireCallback cb;
};

struct DirectTcpConnectState {
  DirectTcpConnectState(ConnectionKey key_in, IoContextExecutor executor,
                        DirectStreamBuilder::AcquireCallback cb_in)
      : key(std::move(key_in)),
        stream(std::move(executor)),
        cb(std::move(cb_in)) {}

  ConnectionKey key;
  TcpStream stream;
  DirectStreamBuilder::AcquireCallback cb;
};

struct DirectSslConnectState {
  DirectSslConnectState(ConnectionKey key_in, IoContextExecutor executor,
                        DirectStreamBuilder::AcquireCallback cb_in)
      : key(std::move(key_in)),
        stream(std::move(executor), *key.ssl_ctx),
        cb(std::move(cb_in)) {}

  ConnectionKey key;
  SslStream stream;
  DirectStreamBuilder::AcquireCallback cb;
};

/** @brief Connect plain TCP and produce a StreamSlot. */
void DoDirectTcpConnect(ConnectionKey key, IoContextExecutor executor,
                        const TcpResolverResults& results,
                        DirectStreamBuilder::AcquireCallback cb) {
  auto state = AllocateShared<DirectTcpConnectState>(
      std::move(key), std::move(executor), std::move(cb));
  state->stream.expires_after(std::chrono::seconds(2));

  state->stream.async_connect(results, [state](boost::system::error_code ec,
                                               const TcpEndpoint&) mutable {
    if (ec) {
      state->cb(ec, StreamSlot{});
      return;
    }
    StreamSlot slot;
    slot.key = std::move(state->key);
    slot.EmplaceTcp(std::move(state->stream));
    slot.http_version = 11;
    auto cb = std::move(state->cb);
    cb({}, std::move(slot));
  });
}

/** @brief Connect SSL (TCP + SNI + handshake) and produce a StreamSlot. */
void DoDirectSslConnect(ConnectionKey key, IoContextExecutor executor,
                        const TcpResolverResults& results,
                        DirectStreamBuilder::AcquireCallback cb) {
  auto state = AllocateShared<DirectSslConnectState>(
      std::move(key), std::move(executor), std::move(cb));
  boost::beast::get_lowest_layer(state->stream)
      .expires_after(std::chrono::seconds(2));

  boost::beast::get_lowest_layer(state->stream)
      .async_connect(results, [state](boost::system::error_code ec,
                                      const TcpEndpoint&) mutable {
        if (ec) {
          state->cb(ec, StreamSlot{});
          return;
        }

        // SNI hostname.
        if (SSL_set_tlsext_host_name(state->stream.native_handle(),
                                     state->key.host.c_str()) != 1) {
          state->cb(
              boost::system::error_code{static_cast<int>(::ERR_get_error()),
                                        boost::asio::error::get_ssl_category()},
              StreamSlot{});
          return;
        }

        // Peer verification.
        if (state->key.verify_peer) {
          state->stream.set_verify_mode(boost::asio::ssl::verify_peer);
          state->stream.set_verify_callback(
              boost::asio::ssl::host_name_verification(state->key.host));
        } else {
          state->stream.set_verify_mode(boost::asio::ssl::verify_none);
        }

        boost::beast::get_lowest_layer(state->stream)
            .expires_after(std::chrono::seconds(2));

        state->stream.async_handshake(
            boost::asio::ssl::stream_base::client,
            [state](boost::system::error_code ec) mutable {
              if (ec) {
                state->cb(ec, StreamSlot{});
                return;
              }
              StreamSlot slot;
              slot.key = std::move(state->key);
              slot.EmplaceSsl(std::move(state->stream));
              slot.sni_hostname = slot.key.host;
              slot.http_version = 11;
              auto cb = std::move(state->cb);
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
  if (key.scheme == "https" && key.ssl_ctx == nullptr) {
    cb(make_error_code(boost::system::errc::invalid_argument), StreamSlot{});
    return;
  }

  auto state = AllocateShared<DirectResolveState>(
      std::move(key), std::move(executor), std::move(cb));

  const std::string resolve_host = state->key.host;
  const std::string resolve_port = state->key.port;

  state->resolver.async_resolve(
      resolve_host, resolve_port,
      [this_shared = shared_from_this(), state](
          boost::system::error_code ec,
          const TcpResolverResults& results) mutable {
        if (ec) {
          state->cb(ec, StreamSlot{});
          return;
        }

        if (state->key.ssl_ctx != nullptr) {
          DoDirectSslConnect(std::move(state->key), state->executor, results,
                             std::move(state->cb));
        } else {
          DoDirectTcpConnect(std::move(state->key), state->executor, results,
                             std::move(state->cb));
        }
      });
}

void DirectStreamBuilder::Return(StreamSlot slot) { slot.Stream().Close(); }

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
          cb({}, std::move(slot));
          return;
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
struct WebSocketResolveState {
  WebSocketResolveState(ConnectionKey key_in, IoContextExecutor executor_in,
                        StreamBuilder::AcquireCallback cb_in)
      : key(std::move(key_in)),
        executor(std::move(executor_in)),
        resolver(executor),
        cb(std::move(cb_in)) {}

  ConnectionKey key;
  IoContextExecutor executor;
  TcpResolver resolver;
  StreamBuilder::AcquireCallback cb;
};

struct WebSocketTcpConnectState {
  WebSocketTcpConnectState(ConnectionKey key_in, IoContextExecutor executor,
                           StreamBuilder::AcquireCallback cb_in)
      : key(std::move(key_in)),
        stream(std::move(executor)),
        cb(std::move(cb_in)) {}

  ConnectionKey key;
  TcpStream stream;
  StreamBuilder::AcquireCallback cb;
};

void DoWebSocketTcpConnect(ConnectionKey key, IoContextExecutor executor,
                           const TcpResolverResults& results,
                           StreamBuilder::AcquireCallback cb) {
  auto state = AllocateShared<WebSocketTcpConnectState>(
      std::move(key), std::move(executor), std::move(cb));
  state->stream.expires_after(std::chrono::seconds(2));

  state->stream.async_connect(results, [state](boost::system::error_code ec,
                                               const TcpEndpoint&) mutable {
    if (ec) {
      state->cb(ec, StreamSlot{});
      return;
    }

    // Return TcpStream with deferred SSL info. WebSocketClientTask owns the
    // WSS TLS handshake and promotes the handshaked transport into the final
    // websocket::stream<SslStream>.
    StreamSlot slot;
    slot.key = state->key;
    slot.EmplaceTcp(std::move(state->stream));
    slot.deferred_ssl_ctx = state->key.ssl_ctx;
    slot.deferred_verify_peer = state->key.verify_peer;
    slot.sni_hostname = state->key.host;
    slot.http_version = 11;
    auto cb = std::move(state->cb);
    cb({}, std::move(slot));
  });
}

}  // namespace

std::shared_ptr<WebSocketStreamBuilder> WebSocketStreamBuilder::Create(
    std::shared_ptr<StreamBuilder> inner, SslContextPtr ssl_ctx) {
  void* raw =
      Allocate(sizeof(WebSocketStreamBuilder), alignof(WebSocketStreamBuilder));
  try {
    auto* builder =
        new (raw) WebSocketStreamBuilder(std::move(inner), std::move(ssl_ctx));
    return {builder,
            [](WebSocketStreamBuilder* ptr) { DestroyDeallocate(ptr); }};
  } catch (...) {
    Deallocate(raw, sizeof(WebSocketStreamBuilder),
               alignof(WebSocketStreamBuilder));
    throw;
  }
}

WebSocketStreamBuilder::WebSocketStreamBuilder(
    std::shared_ptr<StreamBuilder> inner, SslContextPtr ssl_ctx)
    : StreamBuilderDecorator(std::move(inner)), ssl_ctx_(std::move(ssl_ctx)) {}

void WebSocketStreamBuilder::Acquire(ConnectionKey key,
                                     IoContextExecutor executor,
                                     AcquireCallback cb) {
  if (key.scheme == "https" && key.ssl_ctx == nullptr) {
    cb(make_error_code(boost::system::errc::invalid_argument), StreamSlot{});
    return;
  }

  // For non-SSL WebSocket, forward to inner builder (plain TCP).
  if (key.ssl_ctx == nullptr) {
    inner_->Acquire(std::move(key), std::move(executor), std::move(cb));
    return;
  }

  // For WSS: resolve DNS + connect TCP only. TLS is deferred to
  // WebSocketClientTask so it can own the WSS transition and final WebSocket
  // stream construction.
  auto state = AllocateShared<WebSocketResolveState>(
      std::move(key), std::move(executor), std::move(cb));

  const std::string resolve_host = state->key.host;
  const std::string resolve_port = state->key.port;

  state->resolver.async_resolve(resolve_host, resolve_port,
                                [this_shared = shared_from_this(), state](
                                    boost::system::error_code ec,
                                    const TcpResolverResults& results) mutable {
                                  if (ec) {
                                    state->cb(ec, StreamSlot{});
                                    return;
                                  }
                                  DoWebSocketTcpConnect(
                                      std::move(state->key), state->executor,
                                      results, std::move(state->cb));
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
struct ProxyConnectState {
  ProxyConnectState(ConnectionKey original_key_in, TcpStream tcp_stream_in,
                    StreamBuilder::AcquireCallback cb_in)
      : original_key(std::move(original_key_in)),
        tcp_stream(std::move(tcp_stream_in)),
        cb(std::move(cb_in)) {}

  ConnectionKey original_key;
  TcpStream tcp_stream;
  AllocatedString connect_request;
  AllocatedString response_buffer;
  StreamBuilder::AcquireCallback cb;
};

struct ProxyTlsState {
  ProxyTlsState(ConnectionKey original_key_in, TcpStream tcp_stream,
                StreamBuilder::AcquireCallback cb_in)
      : original_key(std::move(original_key_in)),
        ssl_stream(std::move(tcp_stream), *original_key.proxy_ssl_ctx),
        cb(std::move(cb_in)) {}

  ConnectionKey original_key;
  SslStream ssl_stream;
  StreamBuilder::AcquireCallback cb;
};

void DoProxyTlsHandshake(ConnectionKey original_key, TcpStream tcp_stream,
                         StreamBuilder::AcquireCallback cb);

/** @brief Send CONNECT request and read response on the proxy TCP stream. */
void DoProxyConnect(std::shared_ptr<ProxyConnectState> state) {
  // Build CONNECT request.
  const std::string connect_target =
      state->original_key.host + ":" + state->original_key.port;
  state->connect_request = detail::ToAllocatedString("CONNECT ");
  state->connect_request.append(connect_target.data(), connect_target.size());
  state->connect_request.append(" HTTP/1.1\r\nHost: ");
  state->connect_request.append(connect_target.data(), connect_target.size());
  state->connect_request.append("\r\nProxy-Connection: Keep-Alive\r\n");
  if (!state->original_key.proxy_ssl_ctx) {
    // Should not happen, but guard against it.
    state->cb(make_error_code(boost::system::errc::invalid_argument),
              StreamSlot{});
    return;
  }
  state->connect_request.append("\r\n");

  state->tcp_stream.expires_after(std::chrono::seconds(5));

  boost::asio::async_write(
      state->tcp_stream, boost::asio::buffer(state->connect_request),
      [state](boost::system::error_code ec, std::size_t) mutable {
        if (ec) {
          state->cb(ec, StreamSlot{});
          return;
        }

        // Read CONNECT response.
        state->tcp_stream.expires_after(std::chrono::seconds(5));
        boost::asio::async_read_until(
            state->tcp_stream,
            boost::asio::dynamic_buffer(state->response_buffer), "\r\n\r\n",
            [state](boost::system::error_code ec, std::size_t) mutable {
              if (ec) {
                state->cb(ec, StreamSlot{});
                return;
              }

              // Parse status code from "HTTP/1.x STATUS ..."
              const std::string_view resp{state->response_buffer.data(),
                                          state->response_buffer.size()};
              const auto space1 = resp.find(' ');
              if (space1 == std::string::npos || space1 + 1 >= resp.size()) {
                state->cb(make_error_code(boost::system::errc::protocol_error),
                          StreamSlot{});
                return;
              }
              const auto space2 = resp.find(' ', space1 + 1);
              const std::string_view status_sv =
                  resp.substr(space1 + 1, space2 == std::string::npos
                                              ? std::string::npos
                                              : space2 - space1 - 1);
              int status = 0;
              const auto parse_result =
                  std::from_chars(status_sv.data(),
                                  status_sv.data() + status_sv.size(), status);
              if (parse_result.ec != std::errc{}) {
                state->cb(make_error_code(boost::system::errc::protocol_error),
                          StreamSlot{});
                return;
              }
              if (status != 200) {
                state->cb(
                    make_error_code(boost::system::errc::connection_refused),
                    StreamSlot{});
                return;
              }

              // CONNECT succeeded — perform TLS handshake on the tunnel.
              DoProxyTlsHandshake(std::move(state->original_key),
                                  std::move(state->tcp_stream),
                                  std::move(state->cb));
            });
      });
}

/** @brief Perform TLS handshake on the CONNECT tunnel. */
void DoProxyTlsHandshake(ConnectionKey original_key, TcpStream tcp_stream,
                         StreamBuilder::AcquireCallback cb) {
  auto state = AllocateShared<ProxyTlsState>(
      std::move(original_key), std::move(tcp_stream), std::move(cb));

  // SNI hostname = original target host.
  if (SSL_set_tlsext_host_name(state->ssl_stream.native_handle(),
                               state->original_key.host.c_str()) != 1) {
    state->cb(boost::system::error_code{static_cast<int>(::ERR_get_error()),
                                        boost::asio::error::get_ssl_category()},
              StreamSlot{});
    return;
  }

  // Peer verification.
  if (state->original_key.verify_peer) {
    state->ssl_stream.set_verify_mode(boost::asio::ssl::verify_peer);
    state->ssl_stream.set_verify_callback(
        boost::asio::ssl::host_name_verification(state->original_key.host));
  } else {
    state->ssl_stream.set_verify_mode(boost::asio::ssl::verify_none);
  }

  boost::beast::get_lowest_layer(state->ssl_stream)
      .expires_after(std::chrono::seconds(5));

  state->ssl_stream.async_handshake(
      boost::asio::ssl::stream_base::client,
      [state](boost::system::error_code ec) mutable {
        if (ec) {
          state->cb(ec, StreamSlot{});
          return;
        }

        StreamSlot slot;
        slot.key = state->original_key;
        slot.EmplaceSsl(std::move(state->ssl_stream));
        slot.sni_hostname = state->original_key.host;
        slot.http_version = 11;
        auto cb = std::move(state->cb);
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

  if (!slot.HasTcpStream()) {
    cb(make_error_code(boost::system::errc::invalid_argument), StreamSlot{});
    return;
  }

  auto state = AllocateShared<ProxyConnectState>(
      std::move(original_key), std::move(slot.TcpStreamRef()), std::move(cb));
  DoProxyConnect(std::move(state));
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
  if (key.scheme == "https" && key.has_proxy() &&
      key.proxy_ssl_ctx == nullptr) {
    cb(make_error_code(boost::system::errc::invalid_argument), StreamSlot{});
    return;
  }

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
