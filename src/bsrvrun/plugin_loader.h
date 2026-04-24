/**
 * @file plugin_loader.h
 * @brief Shared-library loader for handler/aspect factories.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_PLUGIN_LOADER_H_
#define BSRVCORE_BSRVRUN_PLUGIN_LOADER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "config_types.h"

namespace bsrvcore {
class HttpServer;
class Logger;
}  // namespace bsrvcore

namespace bsrvcore::bsrvrun {
class LoggerFactory;
class ServiceFactory;
}  // namespace bsrvcore::bsrvrun

namespace bsrvcore::runtime {

namespace service_runtime_detail {

struct LoadedServiceRecord {
  std::size_t slot{0};
  void* service{nullptr};
  bsrvcore::bsrvrun::ServiceFactory* factory{nullptr};
  bool destroyed{false};
};

}  // namespace service_runtime_detail

class PluginLoader {
 public:
  PluginLoader() = default;
  ~PluginLoader();

  PluginLoader(const PluginLoader&) = delete;
  PluginLoader& operator=(const PluginLoader&) = delete;

  bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> CreateHandler(
      const FactoryConfig& config) const;

  bsrvcore::OwnedPtr<bsrvcore::HttpRequestAspectHandler> CreateAspect(
      const FactoryConfig& config) const;

  std::shared_ptr<bsrvcore::Logger> CreateLogger(
      const FactoryConfig& config) const;

  void* CreateService(const ServiceConfig& config);

  void DestroyServices(bsrvcore::HttpServer* server);

 private:
  void* GetOrOpenLibrary(const std::string& path) const;

  mutable std::unordered_map<std::string, void*> handles_;
  mutable std::vector<std::string> handle_order_;
  std::vector<service_runtime_detail::LoadedServiceRecord> loaded_services_;
};

}  // namespace bsrvcore::runtime

#endif
