/**
 * @file http_request_handler.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-24
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#ifndef BSRVCORE_HTTP_REQUEST_HANDLER_H_
#define BSRVCORE_HTTP_REQUEST_HANDLER_H_

#include <exception>
#include <memory>

#include "bsrvcore/http_server_task.h"
#include "bsrvcore/logger.h"

namespace bsrvcore {
class HttpServerTask;

class HttpRequestHandler {
 public:
  virtual void Service(std::shared_ptr<HttpServerTask> task) = 0;

  virtual ~HttpRequestHandler() = default;
};

template <typename Fn>
  requires requires(Fn fn, std::shared_ptr<HttpServerTask> task) {
    { fn(task) };
  }
class FunctionRouteHandler : public HttpRequestHandler {
 public:
  explicit FunctionRouteHandler(Fn fn) : fn_(fn) {}

  void Service(std::shared_ptr<HttpServerTask> task) override try {
    fn_(task);
  } catch (const std::exception& e) {
    task->Log(LogLevel::kWarn, e.what());
  }

 private:
  Fn fn_;
};

class EmptyRouteHandler : public HttpRequestHandler {
 public:
  EmptyRouteHandler() = default;

  void Service(std::shared_ptr<HttpServerTask> task) override;
};

}  // namespace bsrvcore

#endif
