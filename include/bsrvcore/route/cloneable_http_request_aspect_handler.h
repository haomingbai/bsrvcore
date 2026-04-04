/**
 * @file cloneable_http_request_aspect_handler.h
 * @brief Cloneable aspect interfaces used by reusable blueprints.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-26
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_ROUTE_CLONEABLE_HTTP_REQUEST_ASPECT_HANDLER_H_
#define BSRVCORE_ROUTE_CLONEABLE_HTTP_REQUEST_ASPECT_HANDLER_H_

#include <concepts>
#include <memory>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/route/http_request_aspect_handler.h"

namespace bsrvcore {

/**
 * @brief Aspect interface that can be deep-cloned for reusable blueprints.
 */
class CloneableHttpRequestAspectHandler
    : public HttpRequestAspectHandler,
      public NonCopyableNonMovable<CloneableHttpRequestAspectHandler> {
 public:
  /**
   * @brief Create a deep copy of this aspect.
   * @return A cloned aspect instance.
   */
  [[nodiscard]] virtual OwnedPtr<CloneableHttpRequestAspectHandler> Clone()
      const = 0;
};

/**
 * @brief Cloneable function-backed aspect for reusable blueprints.
 *
 * @tparam F1 Callable type for pre-service.
 * @tparam F2 Callable type for post-service.
 */
template <typename F1, typename F2>
  requires std::copy_constructible<F1> && std::copy_constructible<F2> &&
               requires(std::shared_ptr<HttpPreServerTask> pre_task,
                        std::shared_ptr<HttpPostServerTask> post_task, F1 fn1,
                        F2 fn2) {
                 { fn1(pre_task) };
                 { fn2(post_task) };
               }
class CloneableFunctionRequestAspectHandler
    : public CloneableHttpRequestAspectHandler,
      public NonCopyableNonMovable<
          CloneableFunctionRequestAspectHandler<F1, F2>> {
 public:
  /**
   * @brief Construct a cloneable function-backed aspect.
   * @param f1 Pre-service callable.
   * @param f2 Post-service callable.
   */
  CloneableFunctionRequestAspectHandler(F1 f1, F2 f2)
      : f1_(std::move(f1)), f2_(std::move(f2)) {}

  /**
   * @brief Invoke the wrapped pre-service callable.
   * @param task Pre-service task.
   */
  void PreService(std::shared_ptr<HttpPreServerTask> task) override {
    f1_(task);
  }

  /**
   * @brief Invoke the wrapped post-service callable.
   * @param task Post-service task.
   */
  void PostService(std::shared_ptr<HttpPostServerTask> task) override {
    f2_(task);
  }

  /**
   * @brief Clone this aspect.
   * @return A deep copy that preserves the wrapped callables.
   */
  [[nodiscard]] OwnedPtr<CloneableHttpRequestAspectHandler> Clone()
      const override {
    return AllocateUnique<CloneableFunctionRequestAspectHandler<F1, F2>>(f1_,
                                                                         f2_);
  }

 private:
  F1 f1_;
  F2 f2_;
};

}  // namespace bsrvcore

#endif
