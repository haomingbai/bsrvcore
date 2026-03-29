/**
 * @file computing_route_handler.cc
 * @brief Worker-pool route handler decorator implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-29
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/internal/route/computing_route_handler.h"

#include <memory>
#include <utility>

namespace bsrvcore::route_internal {

ComputingRouteHandler::ComputingRouteHandler(
    OwnedPtr<HttpRequestHandler> handler)
    : handler_(std::move(handler)) {}

void ComputingRouteHandler::Service(std::shared_ptr<HttpServerTask> task) {
  auto inner = handler_;
  if (!inner) {
    return;
  }

  task->Dispatch([task = std::move(task), inner = std::move(inner)]() mutable {
    inner->Service(std::move(task));
  });
}

OwnedPtr<HttpRequestHandler> WrapComputingHandler(
    OwnedPtr<HttpRequestHandler> handler) {
  if (!handler) {
    return nullptr;
  }

  return AllocateUnique<ComputingRouteHandler>(std::move(handler));
}

}  // namespace bsrvcore::route_internal
