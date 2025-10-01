/**
 * @file http_server_task.h
 * @brief HTTP server task representing a single request-response cycle
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-24
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Represents a complete HTTP request-response cycle, providing access to
 * request data, response building, session management, and asynchronous
 * operations. This is the main interface for request handlers and aspects.
 */

#pragma once

#ifndef BSRVCORE_HTTP_SERVER_TASK_H_
#define BSRVCORE_HTTP_SERVER_TASK_H_

#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields_fwd.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bsrvcore/logger.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

class HttpServerConnection;
class Context;

// Type aliases for Boost.Beast HTTP types
using HttpRequest = boost::beast::http::request<boost::beast::http::string_body,
                                                boost::beast::http::fields>;

using HttpResponse =
    boost::beast::http::response<boost::beast::http::string_body,
                                 boost::beast::http::fields>;

using HttpResponseHeader =
    boost::beast::http::response_header<boost::beast::http::fields>;

using HttpRequestHeader =
    boost::beast::http::request_header<boost::beast::http::fields>;

/**
 * @brief Represents a single HTTP request-response cycle
 *
 * HttpServerTask encapsulates all data and operations for processing
 * an HTTP request and generating a response. It provides:
 * - Access to request data (headers, body, method, etc.)
 * - Response building with flexible output options
 * - Session and context management
 * - Asynchronous operation support
 * - Connection control and logging
 *
 * This is the primary interface used by request handlers and aspect
 * handlers to process HTTP requests.
 *
 * @code
 * // Example usage in request handler
 * void Service(std::shared_ptr<HttpServerTask> task) override {
 *   auto& request = task->GetRequest();
 *   auto& response = task->GetResponse();
 *
 *   // Set response status and body
 *   response.result(boost::beast::http::status::ok);
 *   response.body() = "Hello, World!";
 *   response.prepare_payload();
 *
 *   // Or use convenience methods
 *   task->SetBody("Hello, World!");
 *   task->SetField("Content-Type", "text/plain");
 *
 *   // Access session data
 *   auto session = task->GetSession();
 *   if (session) {
 *     auto user = session->GetAttribute("user");
 *   }
 *
 *   // Log the request
 *   task->Log(LogLevel::Info, "Processed request to " + request.target());
 * }
 * @endcode
 */
class HttpServerTask : NonCopyableNonMovable<HttpServerTask> {
 public:
  /**
   * @brief Get the HTTP request object
   * @return Reference to the HTTP request
   */
  HttpRequest& GetRequest() noexcept;

  /**
   * @brief Get the HTTP response object
   * @return Reference to the HTTP response
   */
  HttpResponse& GetResponse() noexcept;

  /**
   * @brief Get the current session context
   * @return Shared pointer to session context, nullptr if no session
   */
  std::shared_ptr<Context> GetSession() noexcept;

  /**
   * @brief Set timeout for the current session
   * @param timeout Session timeout in milliseconds
   * @return true if session exists and timeout was set
   */
  bool SetSessionTimeout(std::size_t timeout);

  /**
   * @brief Set the response body content
   * @param body Response body string
   */
  void SetBody(std::string body);

  /**
   * @brief Append content to the response body
   * @param body Content to append to existing body
   */
  void AppendBody(const std::string_view body);

  /**
   * @brief Set a response header field by string key
   * @param key Header field name
   * @param value Header field value
   */
  void SetField(const std::string_view key, const std::string_view value);

  /**
   * @brief Set a response header field by Boost.Beast field enum
   * @param key Header field enum
   * @param value Header field value
   */
  void SetField(boost::beast::http::field key, const std::string_view value);

  /**
   * @brief Enable or disable automatic response writing
   * @param value true to enable auto-write, false to disable
   *
   * @note When enabled, the response is automatically sent when the
   *       handler completes. When disabled, manual WriteHeader/WriteBody
   *       calls are required.
   */
  void SetAutowrite(bool value) noexcept;

  /**
   * @brief Enable or disable keep-alive for this connection
   * @param value true to keep connection alive, false to close
   */
  void SetKeepAlive(bool value) noexcept;

  /**
   * @brief Get the request context
   * @return Shared pointer to request context
   */
  std::shared_ptr<Context> GetContext() noexcept;

  /**
   * @brief Log a message with specified level
   * @param level Log level
   * @param message Log message
   */
  void Log(LogLevel level, const std::string_view message);

  /**
   * @brief Write response body to client (manual mode)
   * @param body Response body content
   *
   * @note Only used when auto-write is disabled
   */
  void WriteBody(std::string body);

  /**
   * @brief Write response headers to client (manual mode)
   * @param header Response headers
   *
   * @note Only used when auto-write is disabled
   */
  void WriteHeader(HttpResponseHeader header);

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
   *
   * @code
   * // Example: Post async database query
   * auto future = task->Post(&Database::Query, db, "SELECT * FROM users");
   * future.then([](auto result) {
   *   // Process query result
   * });
   * @endcode
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
   *
   * @code
   * // Example: Set timeout for external API call
   * auto future = task->SetTimer(5000, &ExternalAPI::Call, api, params);
   * auto result = future.get();  // Throws if timeout occurs
   * @endcode
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
   * @brief Check if the underlying connection is still available
   * @return true if connection is open and operational
   */
  bool IsAvailable() noexcept;

  /**
   * @brief Close the connection (for manual connection management)
   */
  void DoClose();

  HttpServerTask(HttpRequest req, std::vector<std::string> params,
                 std::string current_location,
                 std::shared_ptr<HttpServerConnection> conn);

  /**
   * @brief Destructor of the HttpServerTask
   */
  ~HttpServerTask();

 private:
  HttpRequest req_;                             ///< HTTP request data
  HttpResponse resp_;                           ///< HTTP response data
  std::vector<std::string> parameters_;         ///< Extracted route parameters
  std::string current_location_;                ///< Matched route path
  std::shared_ptr<HttpServerConnection> conn_;  ///< Associated connection
  bool keep_alive_;                             ///< Keep-alive flag
  bool autowrite_;                              ///< Auto-write response flag
};

}  // namespace bsrvcore

#endif
