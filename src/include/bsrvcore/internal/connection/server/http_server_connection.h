/**
 * @file http_server_connection.h
 * @brief Abstract base class for HTTP server connection handling
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-25
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Abstract base class for HTTP server connections that provides common
 * functionality for request processing, routing, session management,
 * and asynchronous operations. Derived classes implement transport-specific
 * details for different protocols (TCP, SSL, etc.).
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_CONNECTION_SERVER_HTTP_SERVER_CONNECTION_H_
#define BSRVCORE_INTERNAL_CONNECTION_SERVER_HTTP_SERVER_CONNECTION_H_

#include <atomic>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/string_body.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/internal/core/async_invoke_helpers.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/route/http_route_result.h"

namespace bsrvcore {

namespace internal {
using HandlerAllocator = bsrvcore::Allocator<std::byte>;
}  // namespace internal

class HttpServer;
class Context;

/**
 * @brief Abstract base class for HTTP server connections
 *
 * Provides common infrastructure for HTTP connection handling including:
 * - Request parsing and routing
 * - Session management
 * - Asynchronous operation scheduling
 * - Timer management
 * - Response writing
 *
 * Derived classes implement transport-specific details for different
 * protocols (TCP, SSL, Unix sockets, etc.).
 *
 * @note This is an abstract class - derived classes must implement
 *       all pure virtual methods for specific transport protocols.
 *
 * @code
 * // Example derived class for TCP connections
 * class HttpTcpConnection : public HttpServerConnection {
 * public:
 *   HttpTcpConnection(boost::asio::ip::tcp::socket socket,
 *                     std::shared_ptr<HttpServer> srv)
 *     : HttpServerConnection(socket.get_executor(), srv, 30000, 15000),
 *       stream_(std::move(socket)) {}
 *
 *   void DoReadHeader() override {
 *     // TCP-specific header reading implementation
 *   }
 *
 *   void DoReadBody() override {
 *     // TCP-specific body reading implementation
 *   }
 *
 *   void DoWriteResponse(HttpResponse resp, bool keep_alive,
 *                        std::size_t write_expiry) override {
 *     // TCP-specific response writing
 *   }
 *
 *   // ... implement other pure virtual methods
 *
 * private:
 *   boost::beast::tcp_stream stream_;
 * };
 * @endcode
 */
class HttpServerConnection
    : public NonCopyableNonMovable<HttpServerConnection>,
      public std::enable_shared_from_this<HttpServerConnection> {
 public:
  /**
   * @brief Get per-connection handler allocator (copy).
   *
   * @note Internal API: intended for binding Boost.Asio handlers and for
   *       task factories to allocate request-lifecycle objects.
   */
  internal::HandlerAllocator GetHandlerAllocator() const noexcept {
    return handler_alloc_;
  }

  /**
   * @brief Post a function to the server worker pool.
   * @param fn Function to execute asynchronously.
   *
   * @details
   * This is intended for general/CPU work and follows HttpServer::Post
   * semantics. For short I/O-path sequencing on io_context, use
   * PostToIoContext() or DispatchToIoContext().
   */
  void Post(std::function<void()> fn);

  /**
   * @brief Dispatch a function on the connection's strand.
   * @param fn Function to execute.
   *
   * @details
   * Dispatches onto the server worker pool. For short I/O-context work, use
   * PostToIoContext() or DispatchToIoContext().
   */
  void Dispatch(std::function<void()> fn);

  /**
   * @brief Post a short function onto the server io_context.
   * @param fn Function to execute asynchronously.
   */
  void PostToIoContext(std::function<void()> fn);

  /**
   * @brief Dispatch a short function onto the server io_context.
   * @param fn Function to execute.
   *
   * @details
   * Uses the raw io_context executor and therefore does not preserve any
   * per-connection strand ordering.
   */
  void DispatchToIoContext(std::function<void()> fn);

  /**
   * @brief Dispatch a handler onto the connection strand.
   * @tparam Handler Callable type.
   * @param handler Handler to schedule on the per-connection I/O strand.
   *
   * @details
   * This preserves the handler's associated allocator/executor properties and
   * keeps request-lifecycle phase transitions serialized with connection I/O.
   */
  template <typename Handler>
  void DispatchToConnectionExecutor(Handler&& handler) {
    if (!srv_) {
      return;
    }

    boost::asio::dispatch(strand_, std::forward<Handler>(handler));
  }

  /**
   * @brief Post a function with arguments and return a future for the result
   * @tparam Fn Function type
   * @tparam Args Argument types
   * @param fn Function to execute
   * @param args Arguments to forward to function
   * @return Future containing function result
   */
  template <typename Fn, typename... Args>
  auto FuturedPost(Fn fn, Args&&... args)
      -> std::future<std::invoke_result_t<Fn, Args...>> {
    return internal::async_invoke::StartWithFuture(
        [this](std::function<void()> callback) { Post(std::move(callback)); },
        std::move(fn), std::forward<Args>(args)...);
  }

  /**
   * @brief Post a function with arguments and return a future for the result
   * @tparam Fn Function type
   * @tparam Args Argument types
   * @param fn Function to execute
   * @param args Arguments to forward to function
   */
  template <typename Fn, typename... Args>
  void Post(Fn fn, Args&&... args) {
    internal::async_invoke::StartBound(
        [this](std::function<void()> callback) { Post(std::move(callback)); },
        std::move(fn), std::forward<Args>(args)...);
  }

  /**
   * @brief Set a timer to execute a function after timeout.
   * @param timeout Timeout in milliseconds.
   * @param fn Callback function to execute.
   *
   * @details
   * Follows HttpServer::SetTimer semantics across the full server chain:
   * timer waiting is driven by io_context, callback execution is posted to
   * the server worker pool.
   */
  void SetTimer(std::size_t timeout, std::function<void()> fn);

  /**
   * @brief Set a timer with function and arguments, returning a future
   * @tparam Fn Function type
   * @tparam Args Argument types
   * @param timeout Timeout in milliseconds
   * @param fn Function to execute
   * @param args Arguments to forward to function
   * @return Future containing function result
   */
  template <typename Fn, typename... Args>
  auto SetTimer(std::size_t timeout, Fn fn, Args&&... args)
      -> std::future<std::invoke_result_t<Fn, Args...>> {
    return internal::async_invoke::StartWithFuture(
        [this, timeout](std::function<void()> callback) mutable {
          SetTimer(timeout, std::move(callback));
        },
        std::move(fn), std::forward<Args>(args)...);
  }

  /**
   * @brief Get the connection context
   * @return Shared pointer to connection context
   */
  std::shared_ptr<Context> GetContext() noexcept;

  /**
   * @brief Retrieve a session by ID (copy version)
   * @param sessionid Session identifier
   * @return Shared pointer to session context, nullptr if not found
   */
  std::shared_ptr<Context> GetSession(const std::string& sessionid);

  /**
   * @brief Retrieve a session by ID (move version)
   * @param sessionid Session identifier (will be moved)
   * @return Shared pointer to session context, nullptr if not found
   */
  std::shared_ptr<Context> GetSession(std::string&& sessionid);

  /**
   * @brief Set custom timeout for a session (copy version)
   * @param sessionid Session identifier
   * @param timeout Timeout in milliseconds
   * @return true if session was found and timeout was set
   */
  bool SetSessionTimeout(const std::string& sessionid, std::size_t timeout);

  /**
   * @brief Set custom timeout for a session (move version)
   * @param sessionid Session identifier (will be moved)
   * @param timeout Timeout in milliseconds
   * @return true if session was found and timeout was set
   */
  bool SetSessionTimeout(std::string&& sessionid, std::size_t timeout);

  /**
   * @brief Log a message with specified level
   * @param level Log level
   * @param message Log message
   */
  void Log(LogLevel level, std::string message);

  /**
   * @brief Check if the HTTP server is running
   * @return true if server is running, false otherwise
   */
  bool IsServerRunning() const noexcept;

  /**
   * @brief Check if the underlying stream is available (pure virtual)
   * @return true if stream is open and operational
   */
  virtual bool IsStreamAvailable() const noexcept = 0;

  /**
   * @brief Start the connection processing loop
   */
  void Run();

  /**
   * @brief Write complete HTTP response to client (pure virtual)
   * @param resp HTTP response to write
   * @param keep_alive Whether to keep connection alive
   * @param write_expiry Write timeout in milliseconds for this response
   */
  virtual void DoWriteResponse(HttpResponse resp, bool keep_alive,
                               std::size_t write_expiry) = 0;

  /**
   * @brief Flush response headers to client (pure virtual)
   * @param header HTTP response headers
   */
  virtual void DoFlushResponseHeader(
      boost::beast::http::response_header<boost::beast::http::fields> header,
      std::size_t write_expiry) = 0;

  /**
   * @brief Flush response body to client (pure virtual)
   * @param body Response body content
   */
  virtual void DoFlushResponseBody(std::string body,
                                   std::size_t write_expiry) = 0;

  /**
   * @brief Close the connection (pure virtual)
   */
  virtual void DoClose() = 0;

  /**
   * @brief Process one request cycle (read -> route -> process -> write)
   */
  virtual void DoCycle();

  /**
   * @brief Get the server pointer.
   * @return The pointer of the server.
   */
  HttpServer* GetServer() const noexcept;

  /**
   * @brief Construct a HTTP server connection
   * @param strand ASIO strand for thread-safe operation sequencing
   * @param srv HTTP server instance
   * @param header_read_expiry Header read timeout in milliseconds
   * @param keep_alive_timeout Keep-alive timeout in milliseconds
   * @param has_max_connection Whether connection-cap control is enabled
   * @param available_connection_num Shared approximate available slot counter
   */
  HttpServerConnection(boost::asio::strand<boost::asio::any_io_executor> strand,
                       HttpServer* srv, std::size_t header_read_expiry,
                       std::size_t keep_alive_timeout, bool has_max_connection,
                       std::atomic<std::int64_t>* available_connection_num);

  /**
   * @brief Virtual destructor for proper cleanup
   */
  virtual ~HttpServerConnection();

 protected:
  /**
   * @brief Get the buffer used for reading requests
   * @return Reference to the Beast flat buffer
   */
  boost::beast::flat_buffer& GetBuffer();

  /**
   * @brief Get the executor for this connection
   * @return ASIO strand executor
   */
  boost::asio::strand<boost::asio::any_io_executor> GetExecutor();

  /**
   * @brief Get the HTTP request parser
   * @return Reference to unique pointer holding the request parser
   */
  OwnedPtr<boost::beast::http::request_parser<boost::beast::http::string_body>>&
  GetParser() noexcept;

  /**
   * @brief Route the current request to appropriate handler
   */
  void DoRoute();

  /**
   * @brief Read HTTP request headers (pure virtual)
   */
  virtual void DoReadHeader() = 0;

  /**
   * @brief Read HTTP request body (pure virtual)
   */
  virtual void DoReadBody() = 0;

  /**
   * @brief Wait the for the message in one request to be cleared.
   */
  virtual void ClearMessage() = 0;

  /**
   * @brief Forward request to main handler and aspects for processing
   */
  void DoForwardRequest();

  /**
   * @brief Get the strand for this connection
   * @return Reference to ASIO strand
   */
  boost::asio::strand<boost::asio::any_io_executor>& GetStrand();

  /**
   * @brief Get the timeout of the keep_alive connection.
   * @return timeout in seconds (convenient to be set)
   */
  std::size_t GetKeepAliveTimeout() const noexcept;

  /**
   * @brief Arm the shared connection timer for the current connection phase.
   * @param timeout Timeout in milliseconds; zero disables the timer.
   */
  void ArmTimeout(std::size_t timeout);

  /**
   * @brief Cancel the currently armed connection timer, if any.
   */
  void CancelTimeout();

 private:
  boost::asio::strand<boost::asio::any_io_executor>
      strand_;  ///< Strand for thread-safe operation sequencing
  boost::asio::steady_timer timer_;  ///< Timer for timeouts
  boost::beast::flat_buffer buf_;    ///< Buffer for reading requests
  HttpRouteResult route_result_;     ///< Result of routing the current request
  HttpServer* srv_;                  ///< Reference to HTTP server
  OwnedPtr<boost::beast::http::request_parser<boost::beast::http::string_body>>
      parser_;                      ///< HTTP request parser
  std::size_t header_read_expiry_;  ///< Header read timeout in ms
  std::size_t keep_alive_timeout_;  ///< Keep-alive timeout in ms
  const bool kHasMaxConnection_;    ///< Whether connection-cap control is on.
  std::atomic<std::int64_t>* available_connection_num_{
      nullptr};  ///< Shared approximate available slot counter.

  // Per-connection allocator used for all Asio/Beast handlers.
  internal::HandlerAllocator handler_alloc_;
};

}  // namespace bsrvcore

#endif
