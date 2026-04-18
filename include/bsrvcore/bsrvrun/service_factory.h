/**
 * @file service_factory.h
 * @brief Runtime factory contract for server service providers.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_SERVICE_FACTORY_H_
#define BSRVCORE_BSRVRUN_SERVICE_FACTORY_H_

#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/bsrvrun/plugin_export.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore::bsrvrun {

/**
 * @brief Factory interface used by bsrvrun to create long-lived services.
 *
 * Services are created during runtime assembly, stored in HttpServer service
 * slots as opaque pointers, and destroyed by the same plugin before the shared
 * library is unloaded.
 */
class ServiceFactory : public bsrvcore::NonCopyableNonMovable<ServiceFactory> {
 public:
  /** @brief Create one service instance from runtime parameters. */
  virtual void* GenerateService(ParameterMap* parameters) = 0;

  /** @brief Destroy one service instance previously created by this factory. */
  virtual void DestroyService(void* service) = 0;

  virtual ~ServiceFactory() = default;
};

/**
 * @brief Symbol type of plugin-exported service factory function.
 *
 * Plugins should export
 * `BSRVCORE_BSRVRUN_SERVICE_FACTORY_EXPORT GetServiceFactory();`
 * so the symbol remains visible to runtime loaders on every supported
 * platform.
 */
using GetServiceFactoryFn = ServiceFactory* (*)();

}  // namespace bsrvcore::bsrvrun

#endif
