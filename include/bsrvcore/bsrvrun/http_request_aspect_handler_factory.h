/**
 * @file http_request_aspect_handler_factory.h
 * @brief Runtime factory contract for HTTP request aspects.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_HTTP_REQUEST_ASPECT_HANDLER_FACTORY_H_
#define BSRVCORE_BSRVRUN_HTTP_REQUEST_ASPECT_HANDLER_FACTORY_H_

#include <memory>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/route/http_request_aspect_handler.h"

namespace bsrvcore::bsrvrun {

/**
 * @brief Factory interface used by bsrvrun to create request aspects.
 */
class HttpRequestAspectHandlerFactory {
 public:
  virtual bsrvcore::OwnedPtr<bsrvcore::HttpRequestAspectHandler> Ger(
      ParameterMap* parameters) = 0;

  virtual bsrvcore::OwnedPtr<bsrvcore::HttpRequestAspectHandler> Create(
      ParameterMap* parameters) {
    return Ger(parameters);
  }

  virtual ~HttpRequestAspectHandlerFactory() = default;
};

/**
 * @brief Symbol type of plugin-exported aspect factory function.
 *
 * Plugins should export `extern "C" HttpRequestAspectHandlerFactory*
 * GetAspectFactory();`
 */
using GetAspectFactoryFn = HttpRequestAspectHandlerFactory* (*)();

}  // namespace bsrvcore::bsrvrun

#endif
