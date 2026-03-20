/**
 * @file http_request_handler_factory.h
 * @brief Runtime factory contract for HTTP handlers.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_HTTP_REQUEST_HANDLER_FACTORY_H_
#define BSRVCORE_BSRVRUN_HTTP_REQUEST_HANDLER_FACTORY_H_

#include <memory>

#include "bsrvcore/allocator.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/http_request_handler.h"

namespace bsrvcore::bsrvrun {

/**
 * @brief Factory interface used by bsrvrun to create route handlers.
 */
class HttpRequestHandlerFactory {
 public:
  virtual bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> Ger(
      ParameterMap* parameters) = 0;

  virtual bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> Create(
      ParameterMap* parameters) {
    return Ger(parameters);
  }

  virtual ~HttpRequestHandlerFactory() = default;
};

/**
 * @brief Symbol type of plugin-exported handler factory function.
 *
 * Plugins should export `extern "C" HttpRequestHandlerFactory* GetHandlerFactory();`
 */
using GetHandlerFactoryFn = HttpRequestHandlerFactory* (*)();

}  // namespace bsrvcore::bsrvrun

#endif
