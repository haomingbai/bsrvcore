/**
 * @file http_server.h
 * @brief Main HTTP server class with routing, AOP, and session management
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-26
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Core HTTP server implementation providing comprehensive web service
 * capabilities including routing, aspect-oriented programming, session
 * management, and asynchronous request processing.
 */

#pragma once

#ifndef BSRVCORE_HTTP_SERVER_H_
#define BSRVCORE_HTTP_SERVER_H_

#include <algorithm>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/http/verb.hpp>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "bsrvcore/http_request_aspect_handler.h"
#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_route_result.h"
#include "bsrvcore/http_server_task.h"
#include "bsrvcore/logger.h"
#include "bsrvcore/trait.h"

namespace bthpool::detail {
class BThreadPool;
}  // namespace bthpool::detail

namespace bsrvcore {

class HttpRouteTable;

class SessionMap;

/**
 * @brief Main HTTP server with comprehensive web service capabilities
 *
 * HttpServer provides a full-featured HTTP server implementation with:
 * - RESTful routing with parameter support
 * - Aspect-oriented programming (AOP) for cross-cutting concerns
 * - Session management with configurable timeouts
 * - Asynchronous I/O with timer support
 * - Configurable request limits and timeouts
 * - Keep-alive connection management
 *
 * The class supports fluent interface for configuration and returns
 * HttpServer* for method chaining.
 *
 * @code
 * // Example server setup
 * auto server = std::make_unique<HttpServer>();
 *
 * server->AddRouteEntry(HttpRequestMethod::kGet, "/",
 *                      [](auto task) {
 *                        task->SetBody(200, "Hello World");
 *                      })
 *      ->AddRouteEntry(HttpRequestMethod::kGet, "/users/{id}",
 *                      [](auto task) {
 *                        auto id = task->GetRouteParameters()[0];
 *                        task->SetBody(200, "User: " + id);
 *                      })
 *      ->AddGlobalAspect([](auto task) { // Pre-service
 *                        std::cout << "Request: " << task->GetRequest().path;
 *                      },
 *                      [](auto task) { // Post-service
 *                        std::cout << "Response: " <<
 * task->GetResponse().status;
 *                      })
 *      ->SetDefaultReadExpiry(30000)    // 30 seconds
 *      ->SetDefaultMaxBodySize(1024*1024) // 1MB
 *      ->AddListen(boost::asio::ip::tcp::endpoint(
 *          boost::asio::ip::make_address("0.0.0.0"), 8080))
 *      ->Start(4);  // 4 worker threads
 * @endcode
 */
class HttpServer : public NonCopyableNonMovable<HttpServer> {
 public:
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
   * @brief Post a function to be executed on the server's executor
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
  auto FuturedPost(Fn fn, Args&&... args)
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
   * @brief Post a function with arguments and return a future for the result
   * @tparam Fn Function type
   * @tparam Args Argument types
   * @param fn Function to execute
   * @param args Arguments to forward to function
   */
  template <typename Fn, typename... Args>
  void Post(Fn fn, Args&&... args) {
    auto binded_fn = std::bind(fn, std::forward<Args>(args)...);
    std::function<void()> to_post = [binded_fn]() { binded_fn(); };

    Post(to_post);

    return;
  }

  /**
   * @brief Add a route with a handler object
   * @param method HTTP method
   * @param url Route pattern (supports parameters like {id})
   * @param handler Request handler
   * @return Pointer to server for method chaining
   */
  HttpServer* AddRouteEntry(HttpRequestMethod method,
                            const std::string_view url,
                            std::unique_ptr<HttpRequestHandler> handler);

  /**
   * @brief Add a route with a function object (lambda or function pointer)
   * @tparam Func Callable type accepting std::shared_ptr<HttpServerTask>
   * @param method HTTP method
   * @param str Route pattern
   * @param func Callable to handle requests
   * @return Pointer to server for method chaining
   */
  template <typename Func>
    requires requires(std::shared_ptr<HttpServerTask> task, Func fn) {
      { fn(task) };
    }
  HttpServer* AddRouteEntry(HttpRequestMethod method,
                            const std::string_view str, Func&& func) {
    auto handler = std::make_unique<FunctionRouteHandler<Func>>(func);

    return AddRouteEntry(method, str, std::move(handler));
  }

  /**
   * @brief Add an exclusive route that bypasses parameter routes
   * @param method HTTP method
   * @param url Route pattern
   * @param handler Request handler
   * @return Pointer to server for method chaining
   *
   * @note Exclusive routes take precedence over parameter routes at same path
   */
  HttpServer* AddExclusiveRouteEntry(
      HttpRequestMethod method, const std::string_view url,
      std::unique_ptr<HttpRequestHandler> handler);

  /**
   * @brief Add an exclusive route with a function object
   * @tparam Func Callable type
   * @param method HTTP method
   * @param str Route pattern
   * @param func Callable to handle requests
   * @return Pointer to server for method chaining
   */
  template <typename Func>
    requires requires(std::shared_ptr<HttpServerTask> task, Func fn) {
      { fn(task) };
    }
  HttpServer* AddExclusiveRouteEntry(HttpRequestMethod method,
                                     const std::string_view str, Func&& func) {
    auto handler = std::make_unique<FunctionRouteHandler<Func>>(func);

    return AddExclusiveRouteEntry(method, str, std::move(handler));
  }

  /**
   * @brief Add an aspect handler to a specific route
   * @param method HTTP method
   * @param url Route pattern
   * @param aspect Aspect handler
   * @return Pointer to server for method chaining
   */
  HttpServer* AddAspect(HttpRequestMethod method, const std::string_view url,
                        std::unique_ptr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Add an aspect with function objects for pre/post processing
   * @tparam F1 Pre-service function type
   * @tparam F2 Post-service function type
   * @param method HTTP method
   * @param url Route pattern
   * @param f1 Pre-service function
   * @param f2 Post-service function
   * @return Pointer to server for method chaining
   */
  template <typename F1, typename F2>
  HttpServer* AddAspect(HttpRequestMethod method, const std::string_view url,
                        F1 f1, F2 f2) {
    auto aspect =
        std::make_unique<FunctionRequestAspectHandler<F1, F2>>(f1, f2);

    return AddAspect(method, url, std::move(aspect));
  }

  /**
   * @brief Add a global aspect for a specific HTTP method
   * @param method HTTP method
   * @param aspect Aspect handler
   * @return Pointer to server for method chaining
   */
  HttpServer* AddGlobalAspect(HttpRequestMethod method,
                              std::unique_ptr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Add a global aspect for all HTTP methods
   * @param aspect Aspect handler
   * @return Pointer to server for method chaining
   */
  HttpServer* AddGlobalAspect(std::unique_ptr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Add a global aspect with functions for specific HTTP method
   * @tparam F1 Pre-service function type
   * @tparam F2 Post-service function type
   * @param method HTTP method
   * @param f1 Pre-service function
   * @param f2 Post-service function
   * @return Pointer to server for method chaining
   */
  template <typename F1, typename F2>
  HttpServer* AddGlobalAspect(HttpRequestMethod method, F1 f1, F2 f2) {
    auto aspect =
        std::make_unique<FunctionRequestAspectHandler<F1, F2>>(f1, f2);

    return AddGlobalAspect(method, std::move(aspect));
  }

  /**
   * @brief Add a global aspect with functions for all HTTP methods
   * @tparam F1 Pre-service function type
   * @tparam F2 Post-service function type
   * @param f1 Pre-service function
   * @param f2 Post-service function
   * @return Pointer to server for method chaining
   */
  template <typename F1, typename F2>
  HttpServer* AddGlobalAspect(F1 f1, F2 f2) {
    auto aspect =
        std::make_unique<FunctionRequestAspectHandler<F1, F2>>(f1, f2);

    return AddGlobalAspect(std::move(aspect));
  }

  /**
   * @brief Add a listening endpoint
   * @param ep TCP endpoint to listen on
   * @return Pointer to server for method chaining
   */
  HttpServer* AddListen(boost::asio::ip::tcp::endpoint ep);

  /**
   * @brief Set read timeout for a specific route
   * @param method HTTP method
   * @param url Route pattern
   * @param expiry Read timeout in milliseconds
   * @return Pointer to server for method chaining
   */
  HttpServer* SetReadExpiry(HttpRequestMethod method, std::string_view url,
                            std::size_t expiry);

  /**
   * @brief Set header read timeout for all requests
   * @param expiry Header read timeout in milliseconds
   * @return Pointer to server for method chaining
   */
  HttpServer* SetHeaderReadExpiry(std::size_t expiry);

  /**
   * @brief Set write timeout for a specific route
   * @param method HTTP method
   * @param url Route pattern
   * @param expiry Write timeout in milliseconds
   * @return Pointer to server for method chaining
   */
  HttpServer* SetWriteExpiry(HttpRequestMethod method, std::string_view url,
                             std::size_t expiry);

  /**
   * @brief Set maximum body size for a specific route
   * @param method HTTP method
   * @param url Route pattern
   * @param size Maximum body size in bytes
   * @return Pointer to server for method chaining
   */
  HttpServer* SetMaxBodySize(HttpRequestMethod method, std::string_view url,
                             std::size_t size);

  /**
   * @brief Set default read timeout for all routes
   * @param expiry Read timeout in milliseconds
   * @return Pointer to server for method chaining
   */
  HttpServer* SetDefaultReadExpiry(std::size_t expiry);

  /**
   * @brief Set default write timeout for all routes
   * @param expiry Write timeout in milliseconds
   * @return Pointer to server for method chaining
   */
  HttpServer* SetDefaultWriteExpiry(std::size_t expiry);

  /**
   * @brief Set default maximum body size for all routes
   * @param size Maximum body size in bytes
   * @return Pointer to server for method chaining
   */
  HttpServer* SetDefaultMaxBodySize(std::size_t size);

  /**
   * @brief Set keep-alive connection timeout
   * @param timeout Keep-alive timeout in milliseconds
   * @return Pointer to server for method chaining
   */
  HttpServer* SetKeepAliveTimeout(std::size_t timeout);

  /**
   * @brief Set default request handler for unmatched routes
   * @param handler Global fallback handler for requests
   * @return Pointer to server for method chaining
   */
  HttpServer* SetDefaultHandler(std::unique_ptr<HttpRequestHandler> handler);

  /**
   * @brief Set default request handler with function object
   * @tparam F Callable type accepting std::shared_ptr<HttpServerTask>
   * @param f Global fallback handler function for requests
   * @return Pointer to server for method chaining
   */
  template <typename F>
    requires requires(F f, std::shared_ptr<HttpServerTask> task) {
      { f(task) };
    }
  HttpServer* SetDefaultHandler(F f) {
    std::unique_ptr<HttpRequestHandler> handler =
        std::make_unique<FunctionRouteHandler<F>>(f);
    return SetDefaultHandler(std::move(handler));
  }

  /**
   * @brief Set the SSL context for secure connections
   * @param ctx SSL context for the server
   * @return Pointer to server for method chaining
   */
  HttpServer* SetSslContext(boost::asio::ssl::context ctx);

  /**
   * @brief Unset the SSL context (disable HTTPS)
   * @return Pointer to server for method chaining
   */
  HttpServer* UnsetSslContext();

  /**
   * @brief Set the logger for the server
   * @param logger Shared pointer to the logger
   * @return Pointer to server for method chaining
   */
  HttpServer* SetLogger(std::shared_ptr<Logger> logger);

  /**
   * @brief Log a message with specified level
   * @param level Log level
   * @param message Log message
   */
  void Log(LogLevel level, std::string message);

  /**
   * @brief Route a request to find appropriate handler
   * @param method The method of the request
   * @param target Request path to route
   * @return Routing result with handler and aspects
   */
  HttpRouteResult Route(HttpRequestMethod method, std::string_view target);

  /**
   * @brief Retrieve a session by ID (copy version)
   * @param sessionid Session identifier
   * @return Shared pointer to session context
   */
  std::shared_ptr<Context> GetSession(const std::string& sessionid);

  /**
   * @brief Retrieve a session by ID (move version)
   * @param sessionid Session identifier
   * @return Shared pointer to session context
   */
  std::shared_ptr<Context> GetSession(std::string&& sessionid);

  /**
   * @brief Set default session timeout
   * @param timeout Session timeout in milliseconds
   */
  HttpServer* SetDefaultSessionTimeout(std::size_t timeout);

  /**
   * @brief Set the background cleaner of the session map.
   * @param Whether the sessionmap need a background cleaner.
   * @return The pointer of the server.
   */
  HttpServer* SetSessionCleaner(bool use_cleaner);

  /**
   * @brief Set custom timeout for a session (copy version)
   * @param sessionid Session identifier
   * @param timeout Session timeout
   * @return true if session was found and timeout was set
   */
  bool SetSessionTimeout(const std::string& sessionid, std::size_t timeout);

  /**
   * @brief Set custom timeout for a session (move version)
   * @param sessionid Session identifier
   * @param timeout Session timeout
   * @return true if session was found and timeout was set
   */
  bool SetSessionTimeout(std::string&& sessionid, std::size_t timeout);

  /**
   * @brief Get the server context
   * @return Shared pointer to server context
   */
  std::shared_ptr<Context> GetContext();

  /**
   * @brief Get keep-alive timeout setting
   * @return Keep-alive timeout in milliseconds
   */
  std::size_t GetKeepAliveTimeout();

  /**
   * @brief Check if server is running
   * @return true if server is running, false otherwise
   */
  bool IsRunning();

  /**
   * @brief Stop the server
   */
  void Stop();

  /**
   * @brief Start the server with specified number of threads
   * @param thread_count Number of worker threads
   * @return true if server started successfully
   */
  bool Start(std::size_t thread_count);

  /**
   * @brief Convert Boost.Beast HTTP verb to internal representation
   * @param verb Boost.Beast HTTP verb
   * @return Internal HTTP request method
   */
  static HttpRequestMethod BeastHttpVerbToHttpRequestMethod(
      boost::beast::http::verb verb);

  /**
   * @brief Convert internal HTTP method to Boost.Beast verb
   * @param method Internal HTTP request method
   * @return Boost.Beast HTTP verb
   */
  static boost::beast::http::verb HttpRequestMethodToBeastHttpVerb(
      HttpRequestMethod method);

  /**
   * @brief Construct HttpServer with specified thread pool size
   * @param thread_num Number of threads in thread pool
   */
  HttpServer(std::size_t thread_num);

  /**
   * @brief Construct HttpServer with default thread pool size
   */
  HttpServer();

  /**
   * @brief Destroy HttpServer and cleanup resources
   */
  ~HttpServer();

 private:
  void DoAccept(boost::asio::ip::tcp::acceptor& acc);

  std::optional<boost::asio::ssl::context>
      ssl_ctx_;                          ///< The SSL context of the server
  boost::asio::io_context ioc_;          ///< The I/O context of the server
  std::vector<std::thread> io_threads_;  ///< Threads to run I/O context
  std::vector<boost::asio::ip::tcp::acceptor>
      acceptors_;                     ///< Acceptors to accept sockets
  std::shared_mutex mtx_;             ///< Mutex for thread synchronization
  std::shared_ptr<Context> context_;  ///< Global server context
  std::shared_ptr<Logger> logger_;    ///< Logger for server events
  std::unique_ptr<boost::asio::thread_pool>
      thread_pool_;  ///< Thread pool executor
  std::unique_ptr<bthpool::detail::BThreadPool>
      bth_pool_;  ///< Internal background pool (build-time only dependency)
  std::unique_ptr<HttpRouteTable>
      route_table_;                       ///< Route table for request routing
  std::unique_ptr<SessionMap> sessions_;  ///< Session manager
  std::size_t header_read_expiry_;  ///< Default expiry for reading headers (ms)
  std::size_t keep_alive_timeout_;  ///< Timeout for keep-alive connections (ms)
  std::size_t thread_cnt_;          ///< Number of threads in thread pool
  std::atomic<bool> is_running_;    ///< Flag indicating if server is running
};

}  // namespace bsrvcore

#endif
