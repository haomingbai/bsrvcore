/**
 * @file empty_route_handler.h
 * @brief Default fallback route handler used when no route matches.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-29
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements a minimal HttpRequestHandler that produces a default response.
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_EMPTY_ROUTE_HANDLER_H_
#define BSRVCORE_INTERNAL_EMPTY_ROUTE_HANDLER_H_

#include <memory>

#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

namespace route_internal {

/**
 * @brief Fallback handler used when routing fails.
 */
class EmptyRouteHandler : public HttpRequestHandler,
                          public CopyableMovable<EmptyRouteHandler> {
 public:
  EmptyRouteHandler() = default;

  /**
  * @brief Produce a default response for unmatched routes.
  * @param task Request task to write the response into.
  */
  void Service(std::shared_ptr<HttpServerTask> task) override;

  ~EmptyRouteHandler() override = default;
};

}  // namespace route_internal

}  // namespace bsrvcore

#endif
