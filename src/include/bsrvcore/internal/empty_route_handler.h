/**
 * @file empty_route_handler.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-29
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_EMPTY_ROUTE_HANDLER_H_
#define BSRVCORE_INTERNAL_EMPTY_ROUTE_HANDLER_H_

#include <memory>

#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

namespace route_internal {

class EmptyRouteHandler : public HttpRequestHandler,
                          public CopyableMovable<EmptyRouteHandler> {
 public:
  EmptyRouteHandler() = default;

  void Service(std::shared_ptr<HttpServerTask> task) override;

  ~EmptyRouteHandler() override = default;
};

}  // namespace route_internal

}  // namespace bsrvcore

#endif
