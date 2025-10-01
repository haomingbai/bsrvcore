/**
 * @file empty_route_handler.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-01
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/internal/empty_route_handler.h"

#include <memory>

#include "bsrvcore/http_server_task.h"

using bsrvcore::route_internal::EmptyRouteHandler;

void EmptyRouteHandler::Service(std::shared_ptr<HttpServerTask> task) {
  task->SetBody(R"(
{
  "message": "Service is not available currently",
  "code": 404
}
  )");
  task->SetKeepAlive(false);
}
