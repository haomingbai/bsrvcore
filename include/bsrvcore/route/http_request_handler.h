/**
 * @file http_request_handler.h
 * @brief Interface and adapters for HTTP request handlers
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-24
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines the interface for HTTP request handlers and provides template
 * adapters for creating handlers from function objects and lambdas.
 */

#pragma once

#ifndef BSRVCORE_ROUTE_HTTP_REQUEST_HANDLER_H_
#define BSRVCORE_ROUTE_HTTP_REQUEST_HANDLER_H_

#include <exception>
#include <memory>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore {
class HttpServerTask;

/**
 * @brief Interface for HTTP request handlers
 *
 * HttpRequestHandler defines the contract for classes that process
 * HTTP requests and generate responses. Handlers are responsible for
 * the core business logic of request processing.
 *
 * @code
 * // Example custom handler
 * class UserHandler : public HttpRequestHandler {
 * public:
 *   void Service(const std::shared_ptr<HttpServerTask>& task) override {
 *     const auto& request = task->GetRequest();
 *
 *     if (request.method == HttpRequestMethod::kGet) {
 *       task->GetResponse().result(HttpStatus::ok);
 *       task->SetBody("User data");
 *     } else {
 *       task->GetResponse().result(HttpStatus::method_not_allowed);
 *     }
 *   }
 * };
 *
 * // Register with route table
 * server->AddRouteEntry(HttpRequestMethod::kGet, "/users",
 *                           std::make_unique<UserHandler>());
 * @endcode
 */
class HttpRequestHandler : public NonCopyableNonMovable<HttpRequestHandler> {
 public:
  /**
   * @brief Process an HTTP request and generate response
   * @param task HTTP server task containing request and response objects
   *
   * @note Implementations should read from task->GetRequest() and write status
   *       through task->GetResponse().result(), plus response helpers such as
   *       task->SetBody() and task->SetField().
   */
  virtual void Service(const std::shared_ptr<HttpServerTask>& task) = 0;

  /**
   * @brief Virtual destructor for proper cleanup
   */
  virtual ~HttpRequestHandler() = default;
};

/**
 * @brief Adapter for creating request handlers from function objects
 *
 * This template class allows creating HTTP request handlers from
 * callable objects (functions, lambdas, functors) without needing
 * to create a full class implementation. Provides automatic exception
 * handling with logging.
 *
 * @tparam Fn Type of callable object (must accept
 * const std::shared_ptr<HttpServerTask>&)
 *
 * @code
 * // Example using lambda function
 * auto handler = AllocateUnique<FunctionRouteHandler<
 *   std::function<void(const std::shared_ptr<HttpServerTask>&)>
 * >>(
 *   [](const std::shared_ptr<HttpServerTask>& task) {
 *     task->GetResponse().result(HttpStatus::ok);
 *     task->SetBody("Hello, World!");
 *   }
 * );
 *
 * // Example using free function
 * void HandleRequest(const std::shared_ptr<HttpServerTask>& task) {
 *   // ... process request
 * }
 *
 * auto handler2 = AllocateUnique<FunctionRouteHandler<
 *   decltype(&HandleRequest)
 * >>(&HandleRequest);
 * @endcode
 */
template <typename Fn>
  requires requires(Fn fn, const std::shared_ptr<HttpServerTask>& task) {
    { fn(task) };
  }
class FunctionRouteHandler
    : public HttpRequestHandler,
      public NonCopyableNonMovable<FunctionRouteHandler<Fn>> {
 public:
  /**
   * @brief Construct a function-based route handler
   * @param fn Callable object that processes HTTP requests
   */
  explicit FunctionRouteHandler(Fn fn) : fn_(fn) {}

  /**
   * @brief Process HTTP request by invoking the wrapped function
   * @param task HTTP server task to process
   *
   * @note Automatically catches and logs exceptions to prevent
   *       server crashes from handler errors
   */
  void Service(const std::shared_ptr<HttpServerTask>& task) override try {
    fn_(task);
  } catch (const std::exception& e) {
    task->Log(LogLevel::kWarn, e.what());
  }

 private:
  Fn fn_;  ///< Wrapped callable object
};

}  // namespace bsrvcore

#endif
