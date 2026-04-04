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

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/bsrvrun/plugin_export.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/route/http_request_handler.h"

namespace bsrvcore::bsrvrun {

/**
 * @brief Factory interface used by bsrvrun to create route handlers.
 */
class HttpRequestHandlerFactory
    : public bsrvcore::NonCopyableNonMovable<HttpRequestHandlerFactory> {
 public:
  /** @brief Legacy handler construction entry point used by existing plugins.
   */
  virtual bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> Ger(
      ParameterMap* parameters) = 0;

  /** @brief Preferred handler construction entry point. */
  virtual bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> Create(
      ParameterMap* parameters) {
    return Ger(parameters);
  }

  virtual ~HttpRequestHandlerFactory() = default;
};

/**
 * @brief Symbol type of plugin-exported handler factory function.
 *
 * Plugins should export
 * `BSRVCORE_BSRVRUN_HANDLER_FACTORY_EXPORT GetHandlerFactory();`
 * so the symbol remains visible to `GetProcAddress()` on Windows.
 */
using GetHandlerFactoryFn = HttpRequestHandlerFactory* (*)();

}  // namespace bsrvcore::bsrvrun

#endif
