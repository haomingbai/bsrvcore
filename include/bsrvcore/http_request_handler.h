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

#include "bsrvcore/trait.h"
#ifndef BSRVCORE_HTTP_REQUEST_HANDLER_H_
#define BSRVCORE_HTTP_REQUEST_HANDLER_H_

#include <exception>
#include <memory>

#include "bsrvcore/http_server_task.h"
#include "bsrvcore/logger.h"

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
 *   void Service(std::shared_ptr<HttpServerTask> task) override {
 *     auto& request = task->GetRequest();
 *     auto& response = task->GetResponse();
 *
 *     if (request.method == HttpRequestMethod::GET) {
 *       response.body = "User data";
 *       response.status = 200;
 *     } else {
 *       response.status = 405; // Method Not Allowed
 *     }
 *   }
 * };
 *
 * // Register with route table
 * route_table->AddRouteEntry(HttpRequestMethod::GET, "/users",
 *                           std::make_unique<UserHandler>());
 * @endcode
 */
class HttpRequestHandler {
 public:
  /**
   * @brief Process an HTTP request and generate response
   * @param task HTTP server task containing request and response objects
   *
   * @note Implementations should read from task->GetRequest() and
   *       write to task->GetResponse() or task->SetResponse()
   */
  virtual void Service(std::shared_ptr<HttpServerTask> task) = 0;

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
 * std::shared_ptr<HttpServerTask>)
 *
 * @code
 * // Example using lambda function
 * auto handler = std::make_unique<FunctionRouteHandler<
 *   std::function<void(std::shared_ptr<HttpServerTask>)>
 * >>(
 *   [](std::shared_ptr<HttpServerTask> task) {
 *     task->SetResponse(200, "Hello, World!");
 *   }
 * );
 *
 * // Example using free function
 * void HandleRequest(std::shared_ptr<HttpServerTask> task) {
 *   // ... process request
 * }
 *
 * auto handler2 = std::make_unique<FunctionRouteHandler<
 *   decltype(&HandleRequest)
 * >>(&HandleRequest);
 * @endcode
 */
template <typename Fn>
  requires requires(Fn fn, std::shared_ptr<HttpServerTask> task) {
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
  void Service(std::shared_ptr<HttpServerTask> task) override try {
    fn_(task);
  } catch (const std::exception& e) {
    task->Log(LogLevel::kWarn, e.what());
  }

 private:
  Fn fn_;  ///< Wrapped callable object
};

}  // namespace bsrvcore

#endif
