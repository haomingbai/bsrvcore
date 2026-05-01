/**
 * @file stream_slot.h
 * @brief Connection identity key and reusable stream slot for HTTP client
 * pooling.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-26
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_STREAM_SLOT_H_
#define BSRVCORE_CONNECTION_CLIENT_STREAM_SLOT_H_

#include <boost/asio/ssl/context.hpp>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "bsrvcore/connection/client/client_stream.h"
#include "bsrvcore/core/types.h"

namespace bsrvcore {

/**
 * @brief Hashable connection identity used as pool key.
 *
 * Two connections share the same key when they are interchangeable:
 * same scheme, host, port, and SSL context pointer.
 */
struct ConnectionKey {
  /** @brief URI scheme ("http" or "https"). */
  std::string scheme;
  /** @brief Normalized host. */
  std::string host;
  /** @brief Service port string. */
  std::string port;
  /** @brief TLS context (nullptr for plain HTTP). */
  SslContextPtr ssl_ctx;
  /** @brief Whether to verify TLS peer certificate. */
  bool verify_peer{true};
  /** @brief Proxy server host (empty = no proxy). */
  std::string proxy_host;
  /** @brief Proxy server port (empty = no proxy). */
  std::string proxy_port;
  /**
   * @brief Original SSL context for HTTPS proxy CONNECT tunnel TLS.
   *
   * When ProxyRequestAssembler clears ssl_ctx (so the inner builder
   * connects to the proxy via plain TCP), this field preserves the
   * original SSL context so ProxyStreamBuilder can perform TLS
   * handshake on the CONNECT tunnel.
   */
  SslContextPtr proxy_ssl_ctx;

  /** @brief Whether this connection goes through a proxy. */
  [[nodiscard]] bool has_proxy() const noexcept { return !proxy_host.empty(); }

  /** @brief Equality comparison for unordered container lookup. */
  bool operator==(const ConnectionKey& other) const noexcept {
    return scheme == other.scheme && host == other.host && port == other.port &&
           ssl_ctx == other.ssl_ctx && verify_peer == other.verify_peer &&
           proxy_host == other.proxy_host && proxy_port == other.proxy_port;
  }
};

/**
 * @brief Hash functor for ConnectionKey.
 */
struct ConnectionKeyHash {
  /** @brief Compute combined hash for ConnectionKey. */
  std::size_t operator()(const ConnectionKey& k) const noexcept {
    std::size_t h = 0;
    auto combine = [&h](std::size_t v) {
      h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2);
    };
    combine(std::hash<std::string>{}(k.scheme));
    combine(std::hash<std::string>{}(k.host));
    combine(std::hash<std::string>{}(k.port));
    combine(std::hash<const void*>{}(k.ssl_ctx.get()));
    combine(std::hash<bool>{}(k.verify_peer));
    combine(std::hash<std::string>{}(k.proxy_host));
    combine(std::hash<std::string>{}(k.proxy_port));
    return h;
  }
};

/**
 * @brief A single reusable connection slot.
 *
 * StreamSlot owns the underlying transport(s) and carries metadata
 * used for compatibility checks and idle-timeout decisions.
 */
class StreamSlot {
 public:
  /** @brief Default idle deadline (no specific deadline). */
  StreamSlot() = default;

  /** @brief Move constructor. */
  StreamSlot(StreamSlot&&) noexcept = default;
  /** @brief Move assignment. */
  StreamSlot& operator=(StreamSlot&&) noexcept = default;

  /** @brief Not copyable. */
  StreamSlot(const StreamSlot&) = delete;
  /** @brief Not copyable. */
  StreamSlot& operator=(const StreamSlot&) = delete;

  /**
   * @brief Check whether *this is compatible with the given connection key.
   *
   * Compatibility means the two keys are equal.
   */
  [[nodiscard]] bool IsCompatible(const ConnectionKey& other) const noexcept {
    return key == other;
  }

  /**
   * @brief Whether this slot is still reusable.
   *
   * A slot is reusable when the upstream has not closed the connection and
   * the idle deadline (if set) has not expired.
   */
  [[nodiscard]] bool IsReusable() const noexcept {
    if (!stream_.HasStream()) {
      return false;
    }
    if (upstream_closed) {
      return false;
    }
    if (idle_deadline.has_value()) {
      return std::chrono::steady_clock::now() < *idle_deadline;
    }
    return true;
  }

  [[nodiscard]] ClientStream& Stream() noexcept { return stream_; }

  [[nodiscard]] const ClientStream& Stream() const noexcept { return stream_; }

  [[nodiscard]] bool HasTcpStream() const noexcept { return stream_.IsTcp(); }

  [[nodiscard]] bool HasSslStream() const noexcept { return stream_.IsSsl(); }

  [[nodiscard]] TcpStream& TcpStreamRef() noexcept { return stream_.Tcp(); }

  [[nodiscard]] const TcpStream& TcpStreamRef() const noexcept {
    return stream_.Tcp();
  }

  [[nodiscard]] SslStream& SslStreamRef() noexcept { return stream_.Ssl(); }

  [[nodiscard]] const SslStream& SslStreamRef() const noexcept {
    return stream_.Ssl();
  }

  void EmplaceTcp(TcpStream stream) { stream_.EmplaceTcp(std::move(stream)); }

  void EmplaceSsl(SslStream stream) { stream_.EmplaceSsl(std::move(stream)); }

  void SetStream(ClientStream stream) { stream_ = std::move(stream); }

  ConnectionKey key;
  std::string sni_hostname;
  int http_version{11};
  bool upstream_closed{false};
  std::size_t requests_served{0};
  std::optional<std::chrono::steady_clock::time_point> idle_deadline;

  /**
   * @brief SSL context for deferred TLS handshake (WebSocket WSS).
   *
   * When a WebSocketStreamBuilder returns a TcpStream for WSS, this field
   * carries the SSL context so that WebSocketClientTask can perform the
   * deferred TLS handshake before promoting the stream into the final
   * websocket::stream<SslStream>.
   */
  SslContextPtr deferred_ssl_ctx;

  /**
   * @brief Whether to verify TLS peer for deferred handshake (WebSocket WSS).
   */
  bool deferred_verify_peer{true};

 private:
  ClientStream stream_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_STREAM_SLOT_H_
