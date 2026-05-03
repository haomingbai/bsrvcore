/**
 * @file http_request_aspect_handler.h
 * @brief Aspect-oriented programming interface for HTTP request processing
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-29
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines the interface for HTTP request aspect handlers that provide
 * cross-cutting concerns like authentication, logging, and validation
 * through pre- and post-processing hooks.
 */

#pragma once

#ifndef BSRVCORE_ROUTE_HTTP_REQUEST_ASPECT_HANDLER_H_
#define BSRVCORE_ROUTE_HTTP_REQUEST_ASPECT_HANDLER_H_

#include <memory>

#include "bsrvcore/core/trait.h"

namespace bsrvcore {

class HttpPreServerTask;
class HttpPostServerTask;

/**
 * @brief Interface for HTTP request aspect handlers (AOP)
 *
 * Aspect handlers provide cross-cutting functionality that executes
 * before and/or after the main request handler. Common use cases include:
 * - Authentication and authorization
 * - Request logging and metrics
 * - Input validation and sanitization
 * - Response transformation
 * - Caching and rate limiting
 *
 * @code
 * // Example authentication aspect
 * class AuthAspect : public HttpRequestAspectHandler {
 * public:
 *   void PreService(const std::shared_ptr<HttpPreServerTask>& task) override {
 *     if (task->GetRequest().find(HttpField::authorization) ==
 *         task->GetRequest().end()) {
 *       task->GetResponse().result(HttpStatus::unauthorized);
 *       task->SetBody("Unauthorized");
 *       return;
 *     }
 *     // Continue to main handler
 *   }
 *
 *   void PostService(const std::shared_ptr<HttpPostServerTask>& task) override
 * {
 *     // Log successful authentication
 *     task->Log(LogLevel::kInfo, "Request authenticated successfully");
 *   }
 * };
 * @endcode
 */
class HttpRequestAspectHandler
    : public NonCopyableNonMovable<HttpRequestAspectHandler> {
 public:
  /**
   * @brief Execute before the main request handler
   * @param task Pre-phase task being processed
   *
   * @note Can modify the request and accumulated response. Setting a response
   *       here does not stop later aspects or the main route handler; explicit
   *       short-circuiting would require a separate lifecycle API.
   */
  virtual void PreService(const std::shared_ptr<HttpPreServerTask>& task) = 0;

  /**
   * @brief Execute after the main request handler
   * @param task Post-phase task that was processed
   *
   * @note Can modify the response, log results, or clean up resources
   */
  virtual void PostService(const std::shared_ptr<HttpPostServerTask>& task) = 0;

  /**
   * @brief Virtual destructor for proper cleanup
   */
  virtual ~HttpRequestAspectHandler() = default;
};

/**
 * @brief Adapter for creating aspect handlers from function objects
 *
 * This template class allows creating aspect handlers from lambda functions
 * or function objects, eliminating the need to create full class
 * implementations for simple aspect logic.
 *
 * @tparam F1 Type of pre-service function (should accept
 * const std::shared_ptr<HttpPreServerTask>&)
 * @tparam F2 Type of post-service function (should accept
 * const std::shared_ptr<HttpPostServerTask>&)
 *
 * @code
 * // Example using lambda functions
 * auto logging_aspect = AllocateUnique<FunctionRequestAspectHandler<
 *   std::function<void(const std::shared_ptr<HttpPreServerTask>&)>,
 *   std::function<void(const std::shared_ptr<HttpPostServerTask>&)>
 * >>(
 *   [](const std::shared_ptr<HttpPreServerTask>& task) { // Pre-service
 *     std::cout << "Request: " << task->GetRequest().target() << std::endl;
 *   },
 *   [](const std::shared_ptr<HttpPostServerTask>& task) { // Post-service
 *     std::cout << "Response: " << task->GetResponse().result() << std::endl;
 *   }
 * );
 *
 * // Add to route table
 * route_table->AddGlobalAspect(std::move(logging_aspect));
 * @endcode
 */
template <typename F1, typename F2>
class FunctionRequestAspectHandler
    : public HttpRequestAspectHandler,
      public NonCopyableNonMovable<FunctionRequestAspectHandler<F1, F2>> {
 public:
  /**
   * @brief Execute pre-service function
   * @param task Pre-phase task being processed
   */
  void PreService(const std::shared_ptr<HttpPreServerTask>& task) override {
    f1_(task);
  }

  /**
   * @brief Execute post-service function
   * @param task Post-phase task that was processed
   */
  void PostService(const std::shared_ptr<HttpPostServerTask>& task) override {
    f2_(task);
  }

  /**
   * @brief Construct function-based aspect handler
   * @param f1 Pre-service function
   * @param f2 Post-service function
   */
  FunctionRequestAspectHandler(F1 f1, F2 f2) : f1_(f1), f2_(f2) {}

 private:
  F1 f1_;  ///< Pre-service function object
  F2 f2_;  ///< Post-service function object
};

}  // namespace bsrvcore

#endif
