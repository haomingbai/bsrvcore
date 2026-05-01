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
  ClientWebSocketStream() = default;
  ClientWebSocketStream(ClientWebSocketStream&&) noexcept = default;
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

  [[nodiscard]] bool Empty() const noexcept {
    return std::holds_alternative<std::monostate>(stream_);
  }

  [[nodiscard]] bool HasStream() const noexcept { return !Empty(); }

  [[nodiscard]] bool IsWs() const noexcept {
    return std::holds_alternative<WebSocketStream>(stream_);
  }

  [[nodiscard]] bool IsWss() const noexcept {
    return std::holds_alternative<SecureWebSocketStream>(stream_);
  }

  WebSocketStream& Ws() noexcept {
    assert(IsWs());
    return std::get<WebSocketStream>(stream_);
  }

  const WebSocketStream& Ws() const noexcept {
    assert(IsWs());
    return std::get<WebSocketStream>(stream_);
  }

  SecureWebSocketStream& Wss() noexcept {
    assert(IsWss());
    return std::get<SecureWebSocketStream>(stream_);
  }

  const SecureWebSocketStream& Wss() const noexcept {
    assert(IsWss());
    return std::get<SecureWebSocketStream>(stream_);
  }

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

  void Reset() noexcept { stream_.template emplace<std::monostate>(); }

  template <typename... Args>
  WebSocketStream& EmplaceWs(Args&&... args) {
    return stream_.template emplace<WebSocketStream>(
        std::forward<Args>(args)...);
  }

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
