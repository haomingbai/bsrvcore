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
#include <boost/beast/websocket.hpp>
#include <cstddef>
#include <cstdint>  // NOLINT(misc-include-cleaner): Boost.Beast http headers require std::uint32_t on some toolchains.
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/server_set_cookie.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/core/service_provider.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"
#include "bsrvcore/route/http_route_result.h"

namespace bsrvcore {

class StreamServerConnection;
class Context;
class HttpServer;
class HttpPreServerTask;
class HttpPostServerTask;
class WebSocketHandler;
class WebSocketServerTask;

namespace task_internal {
struct HttpTaskSharedState;
struct HttpPreTaskDeleter;
struct HttpServerTaskDeleter;
struct HttpPostTaskDeleter;
}  // namespace task_internal

enum class HttpTaskConnectionLifecycleMode {
  kAutomatic,
  kManual,
  kWebSocket,
};

/**
 * @brief Shared API surface for lifecycle task phases
 *
 * Provides request/response/session/context/cookie utilities that are shared
 * by pre, service and post phases.
 *
 * @note This class is intended as a base type for phase tasks and is not
 *       created directly by user code.
 */
class HttpTaskBase : public NonCopyableNonMovable<HttpTaskBase> {
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
   * @brief Parse request body as a JSON value.
   * @param out Parsed JSON output.
   * @return Parse error code, or success when parsing completed.
   */
  [[nodiscard]] JsonErrorCode ParseRequestJson(JsonValue& out) const;

  /**
   * @brief Parse request body as a JSON object.
   * @param out Parsed JSON object output.
   * @return Parse/type error code, or success when parsing completed.
   */
  [[nodiscard]] JsonErrorCode ParseRequestJson(JsonObject& out) const;

  /**
   * @brief Parse request body as a JSON value.
   * @param out Parsed JSON output.
   * @return True on success.
   */
  [[nodiscard]] bool TryParseRequestJson(JsonValue& out) const;

  /**
   * @brief Parse request body as a JSON object.
   * @param out Parsed JSON object output.
   * @return True on success.
   */
  [[nodiscard]] bool TryParseRequestJson(JsonObject& out) const;

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
   * @brief Replace response body with serialized JSON.
   * @param value JSON value to serialize.
   */
  void SetJson(const JsonValue& value);

  /**
   * @brief Replace response body with serialized JSON.
   * @param value JSON value to serialize.
   */
  void SetJson(JsonValue&& value);

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
  void SetField(HttpField key, const std::string_view value);

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
   *       Manual mode is a one-way switch from automatic mode only: calling
   *       this API with false has no effect, and calling with true after
   *       WebSocket mode has been selected also has no effect.
   */
  void SetManualConnectionManagement(bool value) noexcept;

  /**
   * @brief Get current connection lifecycle mode.
   * @return Current mode for this task state.
   */
  [[nodiscard]] HttpTaskConnectionLifecycleMode GetConnectionLifecycleMode()
      const noexcept;

  /**
   * @brief Get shared request context.
   * @return Context pointer, or nullptr if unavailable.
   */
  std::shared_ptr<Context> GetContext() noexcept;

  /**
   * @brief Get one configured server service slot.
   * @param index Slot index in the server provider array.
   * @return Opaque non-owning service pointer wrapper.
   */
  [[nodiscard]] ServiceProvider GetServiceProvider(
      std::size_t index) const noexcept;

  /**
   * @brief Get one configured server service slot as a typed pointer.
   * @tparam T Service type shared with the provider plugin.
   * @param index Slot index in the server provider array.
   * @return Typed pointer, or nullptr when the slot is empty.
   */
  template <typename T>
  [[nodiscard]] T* GetService(std::size_t index) const noexcept {
    return GetServiceProvider(index).Get<T>();
  }

  /**
   * @brief Get this request's connection-local io executor.
   * @return Type-erased io executor.
   */
  IoExecutor GetIoExecutor() noexcept;

  /**
   * @brief Get io executors for the current endpoint.
   * @return Endpoint-local io executors, or empty when unavailable.
   */
  std::vector<IoExecutor> GetEndpointExecutors() noexcept;

  /**
   * @brief Get global io executors for all endpoints.
   * @return Global io executors, or empty when unavailable.
   */
  std::vector<IoExecutor> GetGlobalExecutors() noexcept;

  /**
   * @brief Get the server worker executor for async callbacks.
   * @return Type-erased executor backed by the server worker pool.
   */
  IoExecutor GetExecutor() noexcept;

  /**
   * @brief Log message through server logger.
   * @param level Log level.
   * @param message Log content.
   */
  void Log(LogLevel level, const std::string& message);

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
   * @brief Dispatch callback on the server worker executor.
   * @param fn Callback.
   *
   * @details
   * Uses `boost::asio::dispatch` on the worker pool, so the callback may run
   * inline when already executing there.
   */
  void Dispatch(std::function<void()> fn);

  /**
   * @brief Post a short callback onto the connection io executor.
   * @param fn Callback.
   */
  void PostToIoContext(std::function<void()> fn);

  /**
   * @brief Dispatch a short callback onto the connection io executor.
   * @param fn Callback.
   *
   * @details
   * This targets the per-connection io executor.
   */
  void DispatchToIoContext(std::function<void()> fn);

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
   * @brief Check whether current request is a WebSocket upgrade request.
   * @return true when request headers/method satisfy WebSocket upgrade shape.
   */
  bool IsWebSocketRequest() const noexcept;

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
  bool AddCookie(const ServerSetCookie& cookie);

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
  [[nodiscard]] const task_internal::HttpTaskSharedState& GetState()
      const noexcept;

  /**
   * @brief Access shared-state smart pointer.
   * @return Shared-state pointer.
   */
  [[nodiscard]] std::shared_ptr<task_internal::HttpTaskSharedState>
  GetSharedState() const noexcept;

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
      std::shared_ptr<StreamServerConnection> conn);

  /**
   * @brief Start pre-aspect execution.
   */
  void Start();

  /**
   * @brief Destructor.
   */
  ~HttpPreServerTask() override;

 private:
  friend struct task_internal::HttpPreTaskDeleter;
  friend struct task_internal::HttpServerTaskDeleter;
  friend struct task_internal::HttpPostTaskDeleter;

  explicit HttpPreServerTask(
      std::shared_ptr<task_internal::HttpTaskSharedState> state);

  static void RunScheduledPrePhase(
      const std::shared_ptr<HttpPreServerTask>& self);
  void DoPreService(const std::shared_ptr<HttpPreServerTask>& self,
                    std::size_t curr_idx);
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
      std::shared_ptr<StreamServerConnection> conn);

  /**
   * @brief Start route handler execution.
   */
  void Start();

  /**
   * @brief Request HTTP->WebSocket upgrade ownership for current task.
   * @param handler WebSocket callback handler (ownership transferred).
   * @return true when the upgrade handoff has been accepted.
   *
   * @details
   * This API only records upgrade intent and handler ownership. The concrete
   * `WebSocketServerTask` is materialized by the lifecycle deleter once the
   * HTTP post phase fully releases the task.
   */
  bool UpgradeToWebSocket(std::unique_ptr<WebSocketHandler> handler);

  /**
   * @brief Check whether this task has already marked WebSocket upgrade.
   * @return true when upgrade intent has been recorded.
   */
  [[nodiscard]] bool IsWebSocketUpgradeMarked() const noexcept;

  /**
   * @brief Destructor.
   */
  ~HttpServerTask() override;

 private:
  friend struct task_internal::HttpPreTaskDeleter;
  friend struct task_internal::HttpServerTaskDeleter;
  friend struct task_internal::HttpPostTaskDeleter;

  HttpServerTask(HttpRequest req, HttpRouteResult route_result,
                 std::shared_ptr<StreamServerConnection> conn);

  explicit HttpServerTask(
      std::shared_ptr<task_internal::HttpTaskSharedState> state);

  static void RunScheduledServicePhase(
      const std::shared_ptr<HttpServerTask>& self);
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
  ~HttpPostServerTask() override;

 private:
  friend struct task_internal::HttpPreTaskDeleter;
  friend struct task_internal::HttpServerTaskDeleter;
  friend struct task_internal::HttpPostTaskDeleter;

  explicit HttpPostServerTask(
      std::shared_ptr<task_internal::HttpTaskSharedState> state);

  static void RunScheduledPostPhase(
      const std::shared_ptr<HttpPostServerTask>& self, std::size_t curr_idx);
  void DoPostService(const std::shared_ptr<HttpPostServerTask>& self,
                     std::size_t curr_idx);
};

}  // namespace bsrvcore

#endif
