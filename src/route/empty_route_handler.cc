/**
 * @file empty_route_handler.cc
 * @brief EmptyRouteHandler implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-01
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides the default fallback response when no route matches.
 */

#include "internal/empty_route_handler.h"

#include <memory>

#include "bsrvcore/connection/server/http_server_task.h"

using bsrvcore::route_internal::EmptyRouteHandler;

void EmptyRouteHandler::Service(const std::shared_ptr<HttpServerTask>& task) {
  task->SetBody(R"(
{
  "message": "Service is not available currently",
  "code": 404
}
  )");
  task->SetKeepAlive(false);
}
