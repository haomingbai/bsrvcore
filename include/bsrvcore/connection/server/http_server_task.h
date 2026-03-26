/**
 * @file http_server_task.h
 * @brief HTTP server tasks for request lifecycle phases
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-06
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines three lifecycle task types:
 * - HttpPreServerTask: pre-aspect phase
 * - HttpServerTask: route handler phase
 * - HttpPostServerTask: post-aspect phase
 *
 * They share the same request/response state and are connected by custom
 * deleters that transition phase-by-phase.
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_SERVER_HTTP_SERVER_TASK_H_
#define BSRVCORE_CONNECTION_SERVER_HTTP_SERVER_TASK_H_

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/route/http_route_result.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/connection/server/server_set_cookie.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore {

class HttpServerConnection;
class Context;
class HttpServer;
class HttpPreServerTask;
class HttpPostServerTask;

namespace task_internal {
struct HttpTaskSharedState;
struct HttpPreTaskDeleter;
struct HttpServerTaskDeleter;
struct HttpPostTaskDeleter;
}  // namespace task_internal

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
 * @brief Shared API surface for lifecycle task phases
 *
 * Provides request/response/session/context/cookie utilities that are shared
 * by pre, service and post phases.
 *
 * @note This class is intended as a base type for phase tasks and is not
 *       created directly by user code.
 */
class HttpTaskBase {
 public:
  /**
   * @brief Get the HTTP request object.
   * @return Reference to request.
   */
  HttpRequest& GetRequest() noexcept;

  /**
   * @brief Get the HTTP response object.
   * @return Reference to response.
   */
  HttpResponse& GetResponse() noexcept;

  /**
   * @brief Get current session by request sessionId.
   * @return Session context pointer, or nullptr if unavailable.
   *
   * @note If the request does not carry a valid session id cookie, this call
   *       will generate one via GetSessionId() and schedule a Set-Cookie to be
   *       written in the final response.
   */
  std::shared_ptr<Context> GetSession();

  /**
   * @brief Set timeout for current session.
   * @param timeout Timeout in milliseconds.
   * @return true if session timeout was updated.
   */
  bool SetSessionTimeout(std::size_t timeout);

  /**
   * @brief Replace response body.
   * @param body New response body.
   */
  void SetBody(std::string body);

  /**
   * @brief Append content to response body.
   * @param body Content to append.
   */
  void AppendBody(const std::string_view body);

  /**
   * @brief Set response header by key string.
   * @param key Header name.
   * @param value Header value.
   */
  void SetField(const std::string_view key, const std::string_view value);

  /**
   * @brief Set response header by Beast enum key.
   * @param key Header field enum.
   * @param value Header value.
   */
  void SetField(boost::beast::http::field key, const std::string_view value);

  /**
   * @brief Configure keep-alive for final response write.
   * @param value true to keep connection alive.
   */
  void SetKeepAlive(bool value) noexcept;

  /**
   * @brief Enable manual connection lifetime management.
   * @param value true to enable manual mode.
   *
   * @note Once enabled, later phase completion does not auto-write response.
   *       Manual mode is a one-way switch for a task instance: calling this
   *       API with false after it has been enabled has no effect.
   */
  void SetManualConnectionManagement(bool value) noexcept;

  /**
   * @brief Get shared request context.
   * @return Context pointer, or nullptr if unavailable.
   */
  std::shared_ptr<Context> GetContext() noexcept;

  /**
   * @brief Get the IO context of the executor to post IO tasks.
   * @return The IO context of the server.
   */
  boost::asio::io_context& GetIoContext() noexcept;

  /**
   * @brief Get the server worker executor for async callbacks.
   * @return Type-erased executor backed by the server worker pool.
   */
  boost::asio::any_io_executor GetExecutor() noexcept;

  /**
   * @brief Log message through server logger.
   * @param level Log level.
   * @param message Log content.
   */
  void Log(LogLevel level, const std::string message);

  /**
   * @brief Flush response body immediately.
   * @param body Body chunk to flush.
   */
  void WriteBody(std::string body);

  /**
   * @brief Flush response header immediately.
   * @param header Header to flush.
   */
  void WriteHeader(HttpResponseHeader header);

  /**
   * @brief Post callback to server executor.
   * @param fn Callback.
   */
  void Post(std::function<void()> fn);

  /**
   * @brief Post callback and get future result.
   * @tparam Fn Callable type.
   * @tparam Args Argument types.
   * @param fn Callable.
   * @param args Callable arguments.
   * @return Future of callable return value.
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
   * @brief Post callable with arguments.
   * @tparam Fn Callable type.
   * @tparam Args Argument types.
   * @param fn Callable.
   * @param args Callable arguments.
   */
  template <typename Fn, typename... Args>
  void Post(Fn fn, Args&&... args) {
    auto binded_fn = std::bind(fn, std::forward<Args>(args)...);
    std::function<void()> to_post = [binded_fn]() { binded_fn(); };

    Post(to_post);

    return;
  }

  /**
   * @brief Set one-shot timer callback.
   * @param timeout Timeout in milliseconds.
   * @param fn Callback.
   */
  void SetTimer(std::size_t timeout, std::function<void()> fn);

  /**
   * @brief Set one-shot timer callback and get future result.
   * @tparam Fn Callable type.
   * @tparam Args Argument types.
   * @param timeout Timeout in milliseconds.
   * @param fn Callable.
   * @param args Callable arguments.
   * @return Future of callable return value.
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
   * @brief Check whether connection and server are still available.
   * @return true if task can still perform I/O operations.
   */
  bool IsAvailable() noexcept;

  /**
   * @brief Get matched route location string.
   * @return Route location.
   */
  const std::string& GetCurrentLocation();

  /**
   * @brief Get matched route template string.
   * @return Route template.
   */
  const std::string& GetRouteTemplate();

  /**
   * @brief Get cookie by key from request.
   * @param key Cookie name.
   * @return Cookie value reference (empty string if not found).
   *
   * @note When the cookie is not present, this call returns an empty string
   *       and may insert the key into the internal cookie map.
   */
  const std::string& GetCookie(const std::string& key);

  /**
   * @brief Get path parameters extracted by router.
   * @return Path parameter map reference.
   */
  const std::unordered_map<std::string, std::string>& GetPathParameters();

  /**
   * @brief Get one path parameter by name.
   * @param key Parameter name.
   * @return Pointer to parameter value, or nullptr if missing.
   */
  const std::string* GetPathParameter(const std::string& key);

  /**
   * @brief Add Set-Cookie entry to final response.
   * @param cookie Cookie object.
   * @return true if cookie is accepted.
   */
  bool AddCookie(ServerSetCookie cookie);

  /**
   * @brief Close underlying connection.
   */
  void DoClose();

  /**
   * @brief Trigger connection to enter next read cycle.
   */
  void DoCycle();

  /**
   * @brief Get existing sessionId or create one and emit Set-Cookie.
   * @return SessionId string.
   */
  const std::string& GetSessionId();

 protected:
  /**
   * @brief Construct with shared lifecycle state.
   * @param state Shared task state.
   */
  explicit HttpTaskBase(
      std::shared_ptr<task_internal::HttpTaskSharedState> state);

  /**
   * @brief Virtual destructor for derived phases.
   */
  virtual ~HttpTaskBase() = default;

  /**
   * @brief Access mutable shared state.
   * @return Shared state reference.
   */
  task_internal::HttpTaskSharedState& GetState() noexcept;

  /**
   * @brief Access const shared state.
   * @return Shared state const reference.
   */
  const task_internal::HttpTaskSharedState& GetState() const noexcept;

  /**
   * @brief Access shared-state smart pointer.
   * @return Shared-state pointer.
   */
  std::shared_ptr<task_internal::HttpTaskSharedState> GetSharedState()
      const noexcept;

 private:
  void GenerateCookiePairs();
  std::shared_ptr<task_internal::HttpTaskSharedState> state_;
};

/**
 * @brief Pre-aspect phase task
 *
 * Executes all registered `PreService` hooks in registration order.
 */
class HttpPreServerTask
    : public HttpTaskBase,
      public NonCopyableNonMovable<HttpPreServerTask>,
      public std::enable_shared_from_this<HttpPreServerTask> {
 public:
  /**
   * @brief Create pre-phase task with lifecycle-managed deleter chain.
   * @param req Incoming HTTP request.
   * @param route_result Routing result.
   * @param conn Connection for this request.
   * @return Shared pointer to pre-phase task.
   */
  static std::shared_ptr<HttpPreServerTask> Create(
      HttpRequest req, HttpRouteResult route_result,
      std::shared_ptr<HttpServerConnection> conn);

  /**
   * @brief Start pre-aspect execution.
   */
  void Start();

  /**
   * @brief Destructor.
   */
  ~HttpPreServerTask();

 private:
  friend struct task_internal::HttpPreTaskDeleter;
  friend struct task_internal::HttpServerTaskDeleter;
  friend struct task_internal::HttpPostTaskDeleter;

  explicit HttpPreServerTask(
      std::shared_ptr<task_internal::HttpTaskSharedState> state);

  void DoPreService(std::size_t curr_idx);
};

/**
 * @brief Route handler phase task
 *
 * Executes route `HttpRequestHandler::Service`.
 */
class HttpServerTask : public HttpTaskBase,
                       public NonCopyableNonMovable<HttpServerTask>,
                       public std::enable_shared_from_this<HttpServerTask> {
 public:
  /**
   * @brief Create standalone service task.
   *
   * Prefer this factory over constructors to ensure allocations are bound to
   * Boost.Asio's allocator mechanism.
   */
  static std::shared_ptr<HttpServerTask> Create(
      HttpRequest req, HttpRouteResult route_result,
      std::shared_ptr<HttpServerConnection> conn);

  /**
   * @brief Start route handler execution.
   */
  void Start();

  /**
   * @brief Destructor.
   */
  ~HttpServerTask();

 private:
  friend struct task_internal::HttpPreTaskDeleter;
  friend struct task_internal::HttpServerTaskDeleter;
  friend struct task_internal::HttpPostTaskDeleter;

  HttpServerTask(HttpRequest req, HttpRouteResult route_result,
                 std::shared_ptr<HttpServerConnection> conn);

  explicit HttpServerTask(
      std::shared_ptr<task_internal::HttpTaskSharedState> state);

  void DoService();
};

/**
 * @brief Post-aspect phase task
 *
 * Executes all registered `PostService` hooks in reverse order.
 */
class HttpPostServerTask
    : public HttpTaskBase,
      public NonCopyableNonMovable<HttpPostServerTask>,
      public std::enable_shared_from_this<HttpPostServerTask> {
 public:
  /**
   * @brief Start post-aspect execution.
   */
  void Start();

  /**
   * @brief Destructor.
   */
  ~HttpPostServerTask();

 private:
  friend struct task_internal::HttpPreTaskDeleter;
  friend struct task_internal::HttpServerTaskDeleter;
  friend struct task_internal::HttpPostTaskDeleter;

  explicit HttpPostServerTask(
      std::shared_ptr<task_internal::HttpTaskSharedState> state);

  void DoPostService(std::size_t curr_idx);
};

}  // namespace bsrvcore

#endif
