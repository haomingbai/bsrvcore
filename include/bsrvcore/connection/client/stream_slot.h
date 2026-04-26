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
#include <memory>
#include <optional>
#include <string>

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

  /** @brief Equality comparison for unordered container lookup. */
  bool operator==(const ConnectionKey& other) const noexcept {
    return scheme == other.scheme && host == other.host && port == other.port &&
           ssl_ctx == other.ssl_ctx && verify_peer == other.verify_peer;
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
    if (upstream_closed) {
      return false;
    }
    if (idle_deadline.has_value()) {
      return std::chrono::steady_clock::now() < *idle_deadline;
    }
    return true;
  }

  ConnectionKey key;
  std::unique_ptr<TcpStream> tcp_stream;
  std::unique_ptr<SslStream> ssl_stream;
  std::string sni_hostname;
  int http_version{11};
  bool upstream_closed{false};
  std::size_t requests_served{0};
  std::optional<std::chrono::steady_clock::time_point> idle_deadline;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_STREAM_SLOT_H_
