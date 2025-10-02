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

#ifndef BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_H_
#define BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_H_

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/string_body.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "bsrvcore/http_route_result.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "bsrvcore/logger.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

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
 *   void DoWriteResponse(HttpResponse resp, bool keep_alive) override {
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
      protected std::enable_shared_from_this<HttpServerConnection> {
 public:
  /**
   * @brief Post a function to be executed on the connection's strand
   * @param fn Function to execute asynchronously
   */
  void Post(std::function<void()> fn);

  /**
   * @brief Post a function with arguments and return a future for the result
   * @tparam Fn Function type
   * @tparam Args Argument types
   * @param fn Function to execute
   * @param args Arguments to forward to function
   * @return Future containing function result
   */
  template <typename Fn, typename... Args>
  auto Post(Fn fn, Args&&... args)
      -> std::future<std::invoke_result_t<Fn, Args...>> {
    using RT = typename std::invoke_result_t<Fn, Args...>;

    auto binded_fn = std::bind(fn, std::forward<Args>(args)...);
    auto task = std::make_shared<std::packaged_task<RT()>>(binded_fn);
    auto future = task->get_future();
    std::function<void()> to_post = [task]() { (*task)(); };

    Post(to_post);

    return future;
  }

  /**
   * @brief Set a timer to execute a function after timeout
   * @param timeout Timeout in milliseconds
   * @param fn Callback function to execute
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
    using RT = typename std::invoke_result_t<Fn, Args...>;

    auto binded_fn = std::bind(fn, std::forward<Args>(args)...);
    auto task = std::make_shared<std::packaged_task<RT()>>(binded_fn);
    auto future = task->get_future();
    std::function<void()> to_post = [task]() { (*task)(); };

    SetTimer(timeout, to_post);

    return future;
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
   */
  virtual void DoWriteResponse(HttpResponse resp, bool keep_alive) = 0;

  /**
   * @brief Flush response headers to client (pure virtual)
   * @param header HTTP response headers
   */
  virtual void DoFlushResponseHeader(
      boost::beast::http::response_header<boost::beast::http::fields>
          header) = 0;

  /**
   * @brief Flush response body to client (pure virtual)
   * @param body Response body content
   */
  virtual void DoFlushResponseBody(std::string body) = 0;

  /**
   * @brief Close the connection (pure virtual)
   */
  virtual void DoClose() = 0;

  /**
   * @brief Process one request cycle (read -> route -> process -> write)
   */
  void DoCycle();

  /**
   * @brief Construct a HTTP server connection
   * @param strand ASIO strand for thread-safe operation sequencing
   * @param srv HTTP server instance
   * @param header_read_expiry Header read timeout in milliseconds
   * @param keep_alive_timeout Keep-alive timeout in milliseconds
   */
  HttpServerConnection(boost::asio::strand<boost::asio::any_io_executor> strand,
                       std::shared_ptr<HttpServer> srv,
                       std::size_t header_read_expiry,
                       std::size_t keep_alive_timeout);

  /**
   * @brief Virtual destructor for proper cleanup
   */
  virtual ~HttpServerConnection() = default;

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
  boost::asio::strand<boost::asio::io_context::executor_type> GetExecutor();

  /**
   * @brief Get the HTTP request parser
   * @return Reference to unique pointer holding the request parser
   */
  std::unique_ptr<
      boost::beast::http::request_parser<boost::beast::http::string_body>>&
  GetParser() noexcept;

  /**
   * @brief Create HTTP server task for current request
   */
  void MakeHttpServerTask();

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
   * @brief Execute pre-service aspect handlers
   * @param task HTTP server task
   * @param curr_idx Current aspect index in the chain
   */
  void DoPreService(std::shared_ptr<HttpServerTask> task, std::size_t curr_idx);

  /**
   * @brief Execute post-service aspect handlers
   * @param task HTTP server task
   * @param curr_idx Current aspect index in the chain
   */
  void DoPostService(std::shared_ptr<HttpServerTask> task,
                     std::size_t curr_idx);

  /**
   * @brief Forward request to main handler and aspects for processing
   * @param task HTTP server task
   */
  void DoForwardRequest(std::shared_ptr<HttpServerTask> task);

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

 private:
  boost::asio::strand<boost::asio::any_io_executor>
      strand_;  ///< Strand for thread-safe operation sequencing
  boost::asio::steady_timer timer_;  ///< Timer for timeouts
  boost::beast::flat_buffer buf_;    ///< Buffer for reading requests
  HttpRouteResult route_result_;     ///< Result of routing the current request
  std::shared_ptr<HttpServer> srv_;  ///< Reference to HTTP server
  std::unique_ptr<
      boost::beast::http::request_parser<boost::beast::http::string_body>>
      parser_;                      ///< HTTP request parser
  std::size_t header_read_expiry_;  ///< Header read timeout in ms
  std::size_t keep_alive_timeout_;  ///< Keep-alive timeout in ms
};

}  // namespace bsrvcore

#endif
