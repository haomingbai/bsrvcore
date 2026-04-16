/**
 * @file computing_route_handler.h
 * @brief Route handler decorator that dispatches work to the worker pool.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-29
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_ROUTE_COMPUTING_ROUTE_HANDLER_H_
#define BSRVCORE_INTERNAL_ROUTE_COMPUTING_ROUTE_HANDLER_H_

#include <memory>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/route/http_request_handler.h"

namespace bsrvcore::route_internal {

/**
 * @brief Decorates a route handler so its body runs on the worker pool.
 */
class ComputingRouteHandler
    : public HttpRequestHandler,
      public NonCopyableNonMovable<ComputingRouteHandler> {
 public:
  explicit ComputingRouteHandler(OwnedPtr<HttpRequestHandler> handler);

  void Service(const std::shared_ptr<HttpServerTask>& task) override;

  ~ComputingRouteHandler() override = default;

 private:
  OwnedPtr<HttpRequestHandler> handler_;
};

/**
 * @brief Wrap a handler so it runs on the worker pool.
 * @param handler Handler to wrap.
 * @return Wrapped handler, or nullptr when input is nullptr.
 */
OwnedPtr<HttpRequestHandler> WrapComputingHandler(
    OwnedPtr<HttpRequestHandler> handler);

}  // namespace bsrvcore::route_internal

#endif
