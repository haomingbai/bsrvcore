/**
 * @file cloneable_http_request_handler.h
 * @brief Cloneable handler interfaces used by reusable blueprints.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-26
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CLONEABLE_HTTP_REQUEST_HANDLER_H_
#define BSRVCORE_CLONEABLE_HTTP_REQUEST_HANDLER_H_

#include <concepts>
#include <exception>
#include <memory>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/route/http_request_handler.h"

namespace bsrvcore {

/**
 * @brief Handler interface that can be deep-cloned for reusable blueprints.
 */
class CloneableHttpRequestHandler : public HttpRequestHandler {
 public:
  /**
   * @brief Create a deep copy of this handler.
   * @return A cloned handler instance.
   */
  virtual OwnedPtr<CloneableHttpRequestHandler> Clone() const = 0;
};

/**
 * @brief Cloneable function-backed route handler for reusable blueprints.
 *
 * @tparam Fn Callable type accepting `std::shared_ptr<HttpServerTask>`.
 */
template <typename Fn>
  requires std::copy_constructible<Fn> &&
               requires(Fn fn, std::shared_ptr<HttpServerTask> task) {
                 { fn(task) };
               }
class CloneableFunctionRouteHandler
    : public CloneableHttpRequestHandler,
      public NonCopyableNonMovable<CloneableFunctionRouteHandler<Fn>> {
 public:
  /**
   * @brief Construct a cloneable function-backed handler.
   * @param fn Callable to invoke when the route is matched.
   */
  explicit CloneableFunctionRouteHandler(Fn fn) : fn_(std::move(fn)) {}

  /**
   * @brief Invoke the wrapped handler.
   * @param task HTTP request task.
   */
  void Service(std::shared_ptr<HttpServerTask> task) override try {
    fn_(task);
  } catch (const std::exception& e) {
    task->Log(LogLevel::kWarn, e.what());
  }

  /**
   * @brief Clone this handler.
   * @return A deep copy that preserves the wrapped callable.
   */
  OwnedPtr<CloneableHttpRequestHandler> Clone() const override {
    return AllocateUnique<CloneableFunctionRouteHandler<Fn>>(fn_);
  }

 private:
  Fn fn_;
};

}  // namespace bsrvcore

#endif
