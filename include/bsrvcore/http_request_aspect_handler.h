/**
 * @file http_request_aspect_handler.h
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

#ifndef BSRVCORE_HTTP_REQUEST_ASPECT_HANDLER_H_
#define BSRVCORE_HTTP_REQUEST_ASPECT_HANDLER_H_

#include <memory>

namespace bsrvcore {

class HttpServerTask;

class HttpRequestAspectHandler {
 public:
  virtual void PreService(std::shared_ptr<HttpServerTask> task) = 0;

  virtual void PostService(std::shared_ptr<HttpServerTask> task) = 0;

  virtual ~HttpRequestAspectHandler() = default;
};

template <typename F1, typename F2>
class FunctionRequestAspectHandler : public HttpRequestAspectHandler {
 public:
  void PreService(std::shared_ptr<HttpServerTask> task) override { f1_(task); }

  void PostService(std::shared_ptr<HttpServerTask> task) override { f1_(task); }

  FunctionRequestAspectHandler(F1 f1, F2 f2) : f1_(f1), f2_(f2) {}

 private:
  F1 f1_;
  F2 f2_;
};

}  // namespace bsrvcore

#endif
