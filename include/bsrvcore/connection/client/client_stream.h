/**
 * @file client_stream.h
 * @brief Inline variant-backed client TCP/SSL transport wrapper.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-01
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_CLIENT_STREAM_H_
#define BSRVCORE_CONNECTION_CLIENT_CLIENT_STREAM_H_

#include <boost/beast/core/stream_traits.hpp>
#include <cassert>
#include <chrono>
#include <utility>
#include <variant>

#include "bsrvcore/core/types.h"

namespace bsrvcore {

/**
 * @brief Inline owner for one client transport.
 *
 * ClientStream keeps either a TcpStream or an SslStream directly in a variant.
 * Accessors assert the active alternative; callers should branch with IsTcp()
 * or IsSsl() before taking a stream reference.
 */
class ClientStream {
 public:
  ClientStream() = default;
  ClientStream(ClientStream&&) noexcept = default;
  ClientStream& operator=(ClientStream&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (other.IsTcp()) {
      EmplaceTcp(std::move(other.Tcp()));
    } else if (other.IsSsl()) {
      EmplaceSsl(std::move(other.Ssl()));
    } else {
      Reset();
    }
    return *this;
  }

  ClientStream(const ClientStream&) = delete;
  ClientStream& operator=(const ClientStream&) = delete;

  [[nodiscard]] bool Empty() const noexcept {
    return std::holds_alternative<std::monostate>(stream_);
  }

  [[nodiscard]] bool HasStream() const noexcept { return !Empty(); }

  [[nodiscard]] bool IsTcp() const noexcept {
    return std::holds_alternative<TcpStream>(stream_);
  }

  [[nodiscard]] bool IsSsl() const noexcept {
    return std::holds_alternative<SslStream>(stream_);
  }

  TcpStream& Tcp() noexcept {
    assert(IsTcp());
    return std::get<TcpStream>(stream_);
  }

  const TcpStream& Tcp() const noexcept {
    assert(IsTcp());
    return std::get<TcpStream>(stream_);
  }

  SslStream& Ssl() noexcept {
    assert(IsSsl());
    return std::get<SslStream>(stream_);
  }

  const SslStream& Ssl() const noexcept {
    assert(IsSsl());
    return std::get<SslStream>(stream_);
  }

  Tcp::socket& LowestSocket() noexcept {
    assert(HasStream());
    if (IsSsl()) {
      return boost::beast::get_lowest_layer(Ssl()).socket();
    }
    return Tcp().socket();
  }

  template <typename Rep, typename Period>
  void ExpiresAfter(std::chrono::duration<Rep, Period> timeout) {
    assert(HasStream());
    if (IsSsl()) {
      boost::beast::get_lowest_layer(Ssl()).expires_after(timeout);
      return;
    }
    Tcp().expires_after(timeout);
  }

  void Close() noexcept {
    if (Empty()) {
      return;
    }

    boost::system::error_code ignored;
    auto& socket = LowestSocket();
    socket.cancel(ignored);
    socket.shutdown(Tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
  }

  void Reset() noexcept { stream_.template emplace<std::monostate>(); }

  template <typename... Args>
  TcpStream& EmplaceTcp(Args&&... args) {
    return stream_.template emplace<TcpStream>(std::forward<Args>(args)...);
  }

  template <typename... Args>
  SslStream& EmplaceSsl(Args&&... args) {
    return stream_.template emplace<SslStream>(std::forward<Args>(args)...);
  }

 private:
  std::variant<std::monostate, TcpStream, SslStream> stream_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_CLIENT_STREAM_H_
