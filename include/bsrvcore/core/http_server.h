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

#ifndef BSRVCORE_CORE_HTTP_SERVER_H_
#define BSRVCORE_CORE_HTTP_SERVER_H_

#include <algorithm>
#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http/verb.hpp>
#include <cstddef>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/route/http_route_result.h"

namespace bsrvcore {

class HttpRouteTable;
class SessionMap;
class BluePrint;
class ReuseableBluePrint;

/**
 * @brief Parameters used to create server runtime resources.
 *
 * Includes worker-executor knobs and optional connection-cap controls.
 */
struct HttpServerRuntimeOptions {
  std::size_t core_thread_num{std::thread::hardware_concurrency()};
  std::size_t max_thread_num{std::numeric_limits<int>::max()};
  std::size_t fast_queue_capacity{0};
  std::size_t thread_clean_interval{60000};
  std::size_t task_scan_interval{100};
  std::size_t suspend_time{1};
  bool has_max_connection{false};
  std::size_t max_connection{0};
};

using HttpServerExecutorOptions = HttpServerRuntimeOptions;

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
 * auto server = AllocateUnique<HttpServer>();
 *
 * server->AddRouteEntry(HttpRequestMethod::kGet, "/",
 *                      [](auto task) {
 *                        task->GetResponse().result(boost::beast::http::status::ok);
 *                        task->SetBody("Hello World");
 *                      })
 *      ->AddRouteEntry(HttpRequestMethod::kGet, "/users/{id}",
 *                      [](auto task) {
 *                        auto id = task->GetPathParameter("id");
 *                        task->GetResponse().result(boost::beast::http::status::ok);
 *                        task->SetBody("User: " +
 *                                      (id ? *id : std::string("unknown")));
 *                      })
 *      ->AddGlobalAspect([](auto task) { // Pre-service
 *                        std::cout << "Request: " <<
 * task->GetRequest().target();
 *                      },
 *                      [](auto task) { // Post-service
 *                        std::cout << "Response: " <<
 * task->GetResponse().result();
 *                      })
 *      ->SetDefaultReadExpiry(30000)    // 30 seconds
 *      ->SetDefaultMaxBodySize(1024*1024) // 1MB
 *      ->AddListen(boost::asio::ip::tcp::endpoint(
 *          boost::asio::ip::make_address("0.0.0.0"), 8080), 4)
 *      ->Start();
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
    auto task = AllocateShared<std::packaged_task<RT()>>(binded_fn);
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
   * @brief Dispatch a function on the server worker executor.
   * @param fn Function to execute.
   *
   * @details
   * Uses `boost::asio::dispatch` on the worker pool, so the callback may run
   * inline when already executing on that executor.
   */
  void Dispatch(std::function<void()> fn);

  /**
   * @brief Post a short function onto one endpoint I/O executor.
   * @param fn Function to execute asynchronously on an endpoint I/O shard.
   */
  void PostToIoContext(std::function<void()> fn);

  /**
   * @brief Dispatch a short function onto one endpoint I/O executor.
   * @param fn Function to execute on an endpoint I/O shard.
   *
   * @details
   * Uses `boost::asio::dispatch` on a selected endpoint shard executor.
   */
  void DispatchToIoContext(std::function<void()> fn);

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
    auto task = AllocateShared<std::packaged_task<RT()>>(binded_fn);
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
                            OwnedPtr<HttpRequestHandler> handler);

  /**
   * @brief Add a route whose handler body runs on the worker pool.
   * @param method HTTP method.
   * @param url Route pattern.
   * @param handler Request handler to wrap.
   * @return Pointer to server for method chaining.
   *
   * @details
   * The registered handler is wrapped with a decorator that dispatches the
   * route work to the server worker pool while preserving the normal
   * `HttpServerTask` lifecycle semantics.
   */
  HttpServer* AddComputingRouteEntry(HttpRequestMethod method,
                                     const std::string_view url,
                                     OwnedPtr<HttpRequestHandler> handler);

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
    auto handler = AllocateUnique<FunctionRouteHandler<Func>>(func);

    return AddRouteEntry(method, str, std::move(handler));
  }

  /**
   * @brief Add a computing route with a function object.
   * @tparam Func Callable type accepting std::shared_ptr<HttpServerTask>.
   * @param method HTTP method.
   * @param str Route pattern.
   * @param func Callable to handle requests on the worker pool.
   * @return Pointer to server for method chaining.
   */
  template <typename Func>
    requires requires(std::shared_ptr<HttpServerTask> task, Func fn) {
      { fn(task) };
    }
  HttpServer* AddComputingRouteEntry(HttpRequestMethod method,
                                     const std::string_view str, Func&& func) {
    auto handler = AllocateUnique<FunctionRouteHandler<Func>>(func);

    return AddComputingRouteEntry(method, str, std::move(handler));
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
  HttpServer* AddExclusiveRouteEntry(HttpRequestMethod method,
                                     const std::string_view url,
                                     OwnedPtr<HttpRequestHandler> handler);

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
    auto handler = AllocateUnique<FunctionRouteHandler<Func>>(func);

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
                        OwnedPtr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Add an aspect with function objects for pre/post processing
   * @tparam F1 Pre-service function type
   * @tparam F2 Post-service function type
   * @param method HTTP method
   * @param url Route pattern
   * @param f1 Pre-service function (HttpPreServerTask)
   * @param f2 Post-service function (HttpPostServerTask)
   * @return Pointer to server for method chaining
   */
  template <typename F1, typename F2>
    requires requires(std::shared_ptr<HttpPreServerTask> pre_task,
                      std::shared_ptr<HttpPostServerTask> post_task, F1 fn1,
                      F2 fn2) {
      { fn1(pre_task) };
      { fn2(post_task) };
    }
  HttpServer* AddAspect(HttpRequestMethod method, const std::string_view url,
                        F1 f1, F2 f2) {
    auto aspect = AllocateUnique<FunctionRequestAspectHandler<F1, F2>>(f1, f2);

    return AddAspect(method, url, std::move(aspect));
  }

  /**
   * @brief Add a global aspect for a specific HTTP method
   * @param method HTTP method
   * @param aspect Aspect handler
   * @return Pointer to server for method chaining
   */
  HttpServer* AddGlobalAspect(HttpRequestMethod method,
                              OwnedPtr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Add a global aspect for all HTTP methods
   * @param aspect Aspect handler
   * @return Pointer to server for method chaining
   */
  HttpServer* AddGlobalAspect(OwnedPtr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Add a global aspect with functions for specific HTTP method
   * @tparam F1 Pre-service function type
   * @tparam F2 Post-service function type
   * @param method HTTP method
   * @param f1 Pre-service function (HttpPreServerTask)
   * @param f2 Post-service function (HttpPostServerTask)
   * @return Pointer to server for method chaining
   */
  template <typename F1, typename F2>
    requires requires(std::shared_ptr<HttpPreServerTask> pre_task,
                      std::shared_ptr<HttpPostServerTask> post_task, F1 fn1,
                      F2 fn2) {
      { fn1(pre_task) };
      { fn2(post_task) };
    }
  HttpServer* AddGlobalAspect(HttpRequestMethod method, F1 f1, F2 f2) {
    auto aspect = AllocateUnique<FunctionRequestAspectHandler<F1, F2>>(f1, f2);

    return AddGlobalAspect(method, std::move(aspect));
  }

  /**
   * @brief Add a global aspect with functions for all HTTP methods
   * @tparam F1 Pre-service function type
   * @tparam F2 Post-service function type
   * @param f1 Pre-service function (HttpPreServerTask)
   * @param f2 Post-service function (HttpPostServerTask)
   * @return Pointer to server for method chaining
   */
  template <typename F1, typename F2>
    requires requires(std::shared_ptr<HttpPreServerTask> pre_task,
                      std::shared_ptr<HttpPostServerTask> post_task, F1 fn1,
                      F2 fn2) {
      { fn1(pre_task) };
      { fn2(post_task) };
    }
  HttpServer* AddGlobalAspect(F1 f1, F2 f2) {
    auto aspect = AllocateUnique<FunctionRequestAspectHandler<F1, F2>>(f1, f2);

    return AddGlobalAspect(std::move(aspect));
  }

  /**
   * @brief Mount a one-shot blueprint under a path prefix.
   * @param prefix Prefix path where the blueprint root is attached.
   * @param blue_print Blueprint to consume and mount.
   * @return Pointer to server for method chaining.
   */
  HttpServer* AddBluePrint(std::string_view prefix, BluePrint&& blue_print);

  /**
   * @brief Mount a reusable blueprint under a path prefix.
   * @param prefix Prefix path where the blueprint root is attached.
   * @param blue_print Reusable blueprint to clone and mount.
   * @return Pointer to server for method chaining.
   */
  HttpServer* AddBluePrint(std::string_view prefix,
                           const ReuseableBluePrint& blue_print);

  /**
   * @brief Add a listening endpoint and its I/O shard count.
   * @param ep TCP endpoint to listen on.
   * @param io_threads Number of acceptor/io_context shards for this endpoint.
   * @return Pointer to server for method chaining.
   */
  HttpServer* AddListen(boost::asio::ip::tcp::endpoint ep,
                        std::size_t io_threads);

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
  HttpServer* SetDefaultHandler(OwnedPtr<HttpRequestHandler> handler);

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
    OwnedPtr<HttpRequestHandler> handler =
        AllocateUnique<FunctionRouteHandler<F>>(f);
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
   * @param use_cleaner true to enable background session cleanup.
   * @return Pointer to server for method chaining.
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
   * @brief Get one I/O executor selected from endpoint shards.
   * @return A valid executor while running; empty executor otherwise.
   */
  boost::asio::any_io_executor GetIoExecutor() noexcept;

  /**
   * @brief Get a type-erased worker executor for background tasks.
   * @return Any-IO executor backed by the server worker pool.
   */
  boost::asio::any_io_executor GetExecutor() noexcept;

  /**
   * @brief Get all I/O executors for one endpoint.
   * @param endpoint_index Index in AddListen registration order.
   * @return Endpoint executors, or empty when unavailable.
   */
  std::vector<boost::asio::any_io_executor> GetEndpointExecutors(
      std::size_t endpoint_index) noexcept;

  /**
   * @brief Get all endpoint I/O executors in one flat vector.
   * @return Global endpoint executors, or empty when unavailable.
   */
  std::vector<boost::asio::any_io_executor> GetGlobalExecutors() noexcept;

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
   * @brief Start the server.
   * @return true if server started successfully.
   */
  bool Start();

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
   * @brief Construct HttpServer with full runtime options.
   * @param runtime_options Full runtime configuration.
   */
  explicit HttpServer(HttpServerRuntimeOptions runtime_options);

  /**
   * @brief Construct HttpServer with default thread pool size
   */
  HttpServer();

  /**
   * @brief Destroy HttpServer and cleanup resources
   */
  ~HttpServer();

 private:
  struct EndpointListenConfig {
    boost::asio::ip::tcp::endpoint endpoint;
    std::size_t io_threads{1};
  };

  struct EndpointRuntime {
    explicit EndpointRuntime(boost::asio::ip::tcp::endpoint ep)
        : endpoint(std::move(ep)) {}

    boost::asio::ip::tcp::endpoint endpoint;
    std::size_t run_threads{1};
    std::vector<OwnedPtr<boost::asio::io_context>> io_contexts;
    std::vector<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        io_work_guards;
    std::vector<boost::asio::ip::tcp::acceptor> acceptors;
    std::vector<std::thread> io_threads;
  };

  struct ThreadPoolState;

  static OwnedPtr<ThreadPoolState> CreateThreadPool(
      const HttpServerRuntimeOptions& runtime_options);
  boost::asio::any_io_executor GetThreadPoolExecutor() noexcept;
  boost::asio::any_io_executor SelectIoExecutorRoundRobin() noexcept;
  bool BuildEndpointRuntimesLocked(
      std::vector<std::vector<boost::asio::any_io_executor>>& endpoint_execs,
      std::vector<boost::asio::any_io_executor>& global_execs);
  bool BuildFirstEndpointRuntimeLocked(
      const EndpointListenConfig& cfg, OwnedPtr<EndpointRuntime>& runtime,
      std::vector<boost::asio::any_io_executor>& endpoint_execs,
      std::vector<boost::asio::any_io_executor>& global_execs);
  bool BuildReusePortShardsLocked(
      const EndpointListenConfig& cfg, EndpointRuntime& runtime,
      std::size_t start_shard_index,
      std::vector<boost::asio::any_io_executor>& endpoint_execs,
      std::vector<boost::asio::any_io_executor>& global_execs);
  bool BuildReusePortEndpointRuntimeLocked(
      const EndpointListenConfig& cfg, EndpointRuntime& runtime,
      std::vector<boost::asio::any_io_executor>& endpoint_execs,
      std::vector<boost::asio::any_io_executor>& global_execs);
  bool BuildFallbackEndpointRuntimeLocked(
      const EndpointListenConfig& cfg, EndpointRuntime& runtime,
      std::vector<boost::asio::any_io_executor>& endpoint_execs,
      std::vector<boost::asio::any_io_executor>& global_execs);
  void StartEndpointRuntimesLocked();
  void StopEndpointIoLocked();
  void JoinEndpointIoThreadsLocked();
  void PublishExecutorSnapshotsLocked(
      std::vector<std::vector<boost::asio::any_io_executor>> endpoint_execs,
      std::vector<boost::asio::any_io_executor> global_execs);
  void ClearExecutorSnapshotsLocked();
  void ResetControlIoLocked();
  void RollbackStartLocked();
  void JoinThreadPool();
  void ResetThreadPool();
  void StartAcceptedConnection(std::size_t endpoint_index,
                               boost::asio::ip::tcp::socket socket);
  void RearmAcceptIfRunning(std::size_t endpoint_index,
                            std::size_t shard_index);
  void HandleAcceptResult(std::size_t endpoint_index, std::size_t shard_index,
                          boost::system::error_code ec,
                          boost::asio::ip::tcp::socket socket);
  void DoAccept(std::size_t endpoint_index, std::size_t shard_index);

  std::optional<boost::asio::ssl::context>
      ssl_ctx_;  ///< The SSL context of the server
  boost::asio::io_context
      control_ioc_;  ///< Control io_context used by non-connection timers.
  std::optional<
      boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
      control_io_work_guard_;  ///< Keeps control_ioc alive while running.
  std::optional<std::thread>
      control_io_thread_;  ///< Thread running control_ioc.
  std::vector<EndpointListenConfig>
      endpoint_configs_;  ///< Listener declarations configured before Start().
  std::vector<OwnedPtr<EndpointRuntime>>
      endpoint_runtimes_;  ///< Runtime endpoint shards created at Start().
  std::atomic<
      std::shared_ptr<std::vector<std::vector<boost::asio::any_io_executor>>>>
      endpoint_io_execs_snapshot_;  ///< Endpoint-local I/O executor snapshot.
  std::atomic<std::shared_ptr<std::vector<boost::asio::any_io_executor>>>
      global_io_execs_snapshot_;  ///< Flat global endpoint executor snapshot.
  std::atomic<std::size_t> io_exec_round_robin_{
      0};  ///< Round-robin cursor for GetIoExecutor.
  bool reuse_port_supported_{
      false};  ///< Runtime decision: true when REUSEPORT fan-out is enabled.
  std::mutex mtx_;                         ///< Mutex for thread synchronization
  std::shared_ptr<Context> context_;       ///< Global server context
  std::shared_ptr<Logger> logger_;         ///< Logger for server events
  OwnedPtr<ThreadPoolState> thread_pool_;  ///< Worker executor backend
  OwnedPtr<HttpRouteTable> route_table_;   ///< Route table for request routing
  OwnedPtr<SessionMap> sessions_;          ///< Session manager
  std::size_t header_read_expiry_;  ///< Default expiry for reading headers (ms)
  std::size_t keep_alive_timeout_;  ///< Timeout for keep-alive connections (ms)
  const HttpServerRuntimeOptions
      kRuntimeOptions_;           ///< Immutable runtime options.
  const bool kHasMaxConnection_;  ///< Whether max-connection control is on.
  std::atomic<std::int64_t> available_connection_num_{
      0};                         ///< Approximate available connection slots.
  std::atomic<bool> is_running_;  ///< Flag indicating if server is running
};

}  // namespace bsrvcore

#endif
