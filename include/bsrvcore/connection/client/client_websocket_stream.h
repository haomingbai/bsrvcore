/**
 * @file client_websocket_stream.h
 * @brief Inline variant-backed client WebSocket transport wrapper.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-01
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_CLIENT_WEBSOCKET_STREAM_H_
#define BSRVCORE_CONNECTION_CLIENT_CLIENT_WEBSOCKET_STREAM_H_

#include <boost/beast/core/stream_traits.hpp>
#include <cassert>
#include <utility>
#include <variant>

#include "bsrvcore/core/types.h"

namespace bsrvcore {

/**
 * @brief Inline owner for one client WebSocket stream.
 */
class ClientWebSocketStream {
 public:
  /** @brief Construct an empty WebSocket stream wrapper. */
  ClientWebSocketStream() = default;
  /** @brief Move-construct a WebSocket stream wrapper. */
  ClientWebSocketStream(ClientWebSocketStream&&) noexcept = default;
  /**
   * @brief Move-assign while preserving the active stream alternative.
   *
   * @param other Source wrapper to move from.
   * @return Reference to this wrapper.
   */
  ClientWebSocketStream& operator=(ClientWebSocketStream&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (other.IsWs()) {
      EmplaceWs(std::move(other.Ws()));
    } else if (other.IsWss()) {
      EmplaceWss(std::move(other.Wss()));
    } else {
      Reset();
    }
    return *this;
  }

  ClientWebSocketStream(const ClientWebSocketStream&) = delete;
  ClientWebSocketStream& operator=(const ClientWebSocketStream&) = delete;

  /**
   * @brief Return true when no WebSocket stream is stored.
   *
   * @return True when the wrapper is empty.
   */
  [[nodiscard]] bool Empty() const noexcept {
    return std::holds_alternative<std::monostate>(stream_);
  }

  /**
   * @brief Return true when either ws or wss stream is stored.
   *
   * @return True when a stream is active.
   */
  [[nodiscard]] bool HasStream() const noexcept { return !Empty(); }

  /**
   * @brief Return true when the stored stream is plaintext WebSocket.
   *
   * @return True when Ws() may be used.
   */
  [[nodiscard]] bool IsWs() const noexcept {
    return std::holds_alternative<WebSocketStream>(stream_);
  }

  /**
   * @brief Return true when the stored stream is secure WebSocket.
   *
   * @return True when Wss() may be used.
   */
  [[nodiscard]] bool IsWss() const noexcept {
    return std::holds_alternative<SecureWebSocketStream>(stream_);
  }

  /**
   * @brief Return mutable plaintext WebSocket stream; requires IsWs().
   *
   * @return Mutable WebSocket stream reference.
   */
  WebSocketStream& Ws() noexcept {
    assert(IsWs());
    return std::get<WebSocketStream>(stream_);
  }

  /**
   * @brief Return const plaintext WebSocket stream; requires IsWs().
   *
   * @return Const WebSocket stream reference.
   */
  const WebSocketStream& Ws() const noexcept {
    assert(IsWs());
    return std::get<WebSocketStream>(stream_);
  }

  /**
   * @brief Return mutable secure WebSocket stream; requires IsWss().
   *
   * @return Mutable secure WebSocket stream reference.
   */
  SecureWebSocketStream& Wss() noexcept {
    assert(IsWss());
    return std::get<SecureWebSocketStream>(stream_);
  }

  /**
   * @brief Return const secure WebSocket stream; requires IsWss().
   *
   * @return Const secure WebSocket stream reference.
   */
  const SecureWebSocketStream& Wss() const noexcept {
    assert(IsWss());
    return std::get<SecureWebSocketStream>(stream_);
  }

  /** @brief Close the active WebSocket transport if present. */
  void Close() noexcept {
    if (Empty()) {
      return;
    }

    boost::system::error_code ignored;
    auto& socket = IsWss() ? boost::beast::get_lowest_layer(Wss()).socket()
                           : boost::beast::get_lowest_layer(Ws()).socket();
    socket.shutdown(Tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
  }

  /** @brief Reset the wrapper to empty state. */
  void Reset() noexcept { stream_.template emplace<std::monostate>(); }

  /**
   * @brief Construct a plaintext WebSocket stream in-place and return it.
   *
   * @param args Constructor arguments forwarded to WebSocketStream.
   * @return Reference to the newly active WebSocket stream.
   */
  template <typename... Args>
  WebSocketStream& EmplaceWs(Args&&... args) {
    return stream_.template emplace<WebSocketStream>(
        std::forward<Args>(args)...);
  }

  /**
   * @brief Construct a secure WebSocket stream in-place and return it.
   *
   * @param args Constructor arguments forwarded to SecureWebSocketStream.
   * @return Reference to the newly active secure WebSocket stream.
   */
  template <typename... Args>
  SecureWebSocketStream& EmplaceWss(Args&&... args) {
    return stream_.template emplace<SecureWebSocketStream>(
        std::forward<Args>(args)...);
  }

 private:
  std::variant<std::monostate, WebSocketStream, SecureWebSocketStream> stream_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_CLIENT_WEBSOCKET_STREAM_H_
