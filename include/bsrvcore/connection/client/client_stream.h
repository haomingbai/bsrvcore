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
  /** @brief Construct an empty stream wrapper. */
  ClientStream() = default;
  /** @brief Move-construct a stream wrapper. */
  ClientStream(ClientStream&&) noexcept = default;
  /**
   * @brief Move-assign while preserving the active stream alternative.
   *
   * @param other Source wrapper to move from.
   * @return Reference to this wrapper.
   */
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

  /**
   * @brief Return true when no transport is stored.
   *
   * @return True when the wrapper is empty.
   */
  [[nodiscard]] bool Empty() const noexcept {
    return std::holds_alternative<std::monostate>(stream_);
  }

  /**
   * @brief Return true when either TCP or SSL transport is stored.
   *
   * @return True when a transport is active.
   */
  [[nodiscard]] bool HasStream() const noexcept { return !Empty(); }

  /**
   * @brief Return true when the stored transport is plaintext TCP.
   *
   * @return True when Tcp() may be used.
   */
  [[nodiscard]] bool IsTcp() const noexcept {
    return std::holds_alternative<TcpStream>(stream_);
  }

  /**
   * @brief Return true when the stored transport is SSL over TCP.
   *
   * @return True when Ssl() may be used.
   */
  [[nodiscard]] bool IsSsl() const noexcept {
    return std::holds_alternative<SslStream>(stream_);
  }

  /**
   * @brief Return mutable TCP stream; requires IsTcp().
   *
   * @return Mutable TCP stream reference.
   */
  TcpStream& Tcp() noexcept {
    assert(IsTcp());
    return std::get<TcpStream>(stream_);
  }

  /**
   * @brief Return const TCP stream; requires IsTcp().
   *
   * @return Const TCP stream reference.
   */
  const TcpStream& Tcp() const noexcept {
    assert(IsTcp());
    return std::get<TcpStream>(stream_);
  }

  /**
   * @brief Return mutable SSL stream; requires IsSsl().
   *
   * @return Mutable SSL stream reference.
   */
  SslStream& Ssl() noexcept {
    assert(IsSsl());
    return std::get<SslStream>(stream_);
  }

  /**
   * @brief Return const SSL stream; requires IsSsl().
   *
   * @return Const SSL stream reference.
   */
  const SslStream& Ssl() const noexcept {
    assert(IsSsl());
    return std::get<SslStream>(stream_);
  }

  /**
   * @brief Return the lowest TCP socket for the active transport.
   *
   * @return Mutable lowest-layer TCP socket.
   */
  Tcp::socket& LowestSocket() noexcept {
    assert(HasStream());
    if (IsSsl()) {
      return boost::beast::get_lowest_layer(Ssl()).socket();
    }
    return Tcp().socket();
  }

  /**
   * @brief Set the active transport's expiry timeout.
   *
   * @param timeout Timeout duration applied to the active stream.
   */
  template <typename Rep, typename Period>
  void ExpiresAfter(std::chrono::duration<Rep, Period> timeout) {
    assert(HasStream());
    if (IsSsl()) {
      boost::beast::get_lowest_layer(Ssl()).expires_after(timeout);
      return;
    }
    Tcp().expires_after(timeout);
  }

  /** @brief Close and cancel the active transport if present. */
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

  /** @brief Reset the wrapper to empty state. */
  void Reset() noexcept { stream_.template emplace<std::monostate>(); }

  /**
   * @brief Construct a TCP stream in-place and return it.
   *
   * @param args Constructor arguments forwarded to TcpStream.
   * @return Reference to the newly active TCP stream.
   */
  template <typename... Args>
  TcpStream& EmplaceTcp(Args&&... args) {
    return stream_.template emplace<TcpStream>(std::forward<Args>(args)...);
  }

  /**
   * @brief Construct an SSL stream in-place and return it.
   *
   * @param args Constructor arguments forwarded to SslStream.
   * @return Reference to the newly active SSL stream.
   */
  template <typename... Args>
  SslStream& EmplaceSsl(Args&&... args) {
    return stream_.template emplace<SslStream>(std::forward<Args>(args)...);
  }

 private:
  std::variant<std::monostate, TcpStream, SslStream> stream_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_CLIENT_STREAM_H_
