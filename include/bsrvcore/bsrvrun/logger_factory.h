/**
 * @file logger_factory.h
 * @brief Runtime factory contract for bsrvrun loggers.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-19
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_LOGGER_FACTORY_H_
#define BSRVCORE_BSRVRUN_LOGGER_FACTORY_H_

#include <memory>

#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/bsrvrun/plugin_export.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore::bsrvrun {

/**
 * @brief Factory interface used by bsrvrun to create server loggers.
 *
 * The returned shared pointer is passed directly to `HttpServer::SetLogger()`
 * and therefore follows the same lifetime as the server instance.
 */
class LoggerFactory : public bsrvcore::NonCopyableNonMovable<LoggerFactory> {
 public:
  /**
   * @brief Create one logger instance from runtime parameters.
   *
   * @param parameters Runtime configuration parameters for the logger.
   * @return Logger instance to install on the server.
   */
  [[nodiscard]] virtual std::shared_ptr<bsrvcore::Logger> Create(
      ParameterMap* parameters) = 0;

  virtual ~LoggerFactory() = default;
};

/**
 * @brief Symbol type of plugin-exported logger factory function.
 *
 * Plugins should export
 * `BSRVCORE_BSRVRUN_LOGGER_FACTORY_EXPORT GetLoggerFactory();`
 * so the symbol remains visible to runtime loaders on every supported
 * platform.
 */
using GetLoggerFactoryFn = LoggerFactory* (*)();

}  // namespace bsrvcore::bsrvrun

#endif
