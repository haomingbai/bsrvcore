/**
 * @file plugin_loader.cc
 * @brief Shared-library loader for handler/aspect factories.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "plugin_loader.h"

#include <dlfcn.h>

#include <stdexcept>
#include <string>

#include "bsrvcore/bsrvrun/http_request_aspect_handler_factory.h"
#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "parameter_map_impl.h"

namespace bsrvcore::runtime {

namespace {

RuntimeParameterMap BuildMap(const FactoryConfig& config) {
  RuntimeParameterMap params;
  for (const auto& [key, value] : config.params) {
    params.SetRaw(key, value);
  }
  return params;
}

}  // namespace

PluginLoader::~PluginLoader() {
  for (auto it = handle_order_.rbegin(); it != handle_order_.rend(); ++it) {
    auto handle_it = handles_.find(*it);
    if (handle_it != handles_.end() && handle_it->second != nullptr) {
      dlclose(handle_it->second);
    }
  }
}

void* PluginLoader::GetOrOpenLibrary(const std::string& path) const {
  const auto it = handles_.find(path);
  if (it != handles_.end()) {
    // Reuse one dlopen() handle per shared object so repeated factory creation
    // does not reload plugin code or invalidate previously returned factories.
    return it->second;
  }

  dlerror();
  void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* error = dlerror();
    throw std::runtime_error("failed to load library `" + path +
                             "`: " + (error == nullptr ? "unknown" : error));
  }

  handles_.emplace(path, handle);
  handle_order_.emplace_back(path);
  return handle;
}

bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> PluginLoader::CreateHandler(
    const FactoryConfig& config) const {
  void* handle = GetOrOpenLibrary(config.library);

  dlerror();
  void* symbol = dlsym(handle, "GetHandlerFactory");
  const char* symbol_error = dlerror();
  if (symbol_error != nullptr || symbol == nullptr) {
    throw std::runtime_error("`GetHandlerFactory` not found in `" +
                             config.library + "`");
  }

  auto fn = reinterpret_cast<bsrvcore::bsrvrun::GetHandlerFactoryFn>(symbol);
  bsrvcore::bsrvrun::HttpRequestHandlerFactory* factory = fn();
  if (factory == nullptr) {
    throw std::runtime_error("GetHandlerFactory returned nullptr in `" +
                             config.library + "`");
  }

  // Build a fresh ParameterMap per creation call so factories can treat it as
  // request-local configuration instead of shared mutable plugin state.
  RuntimeParameterMap params = BuildMap(config);
  auto handler = factory->Ger(&params);
  if (!handler) {
    throw std::runtime_error("handler factory returned nullptr in `" +
                             config.library + "`");
  }

  return handler;
}

bsrvcore::OwnedPtr<bsrvcore::HttpRequestAspectHandler>
PluginLoader::CreateAspect(const FactoryConfig& config) const {
  void* handle = GetOrOpenLibrary(config.library);

  dlerror();
  void* symbol = dlsym(handle, "GetAspectFactory");
  const char* symbol_error = dlerror();
  if (symbol_error != nullptr || symbol == nullptr) {
    throw std::runtime_error("`GetAspectFactory` not found in `" +
                             config.library + "`");
  }

  auto fn = reinterpret_cast<bsrvcore::bsrvrun::GetAspectFactoryFn>(symbol);
  bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory* factory = fn();
  if (factory == nullptr) {
    throw std::runtime_error("GetAspectFactory returned nullptr in `" +
                             config.library + "`");
  }

  RuntimeParameterMap params = BuildMap(config);
  auto aspect = factory->Ger(&params);
  if (!aspect) {
    throw std::runtime_error("aspect factory returned nullptr in `" +
                             config.library + "`");
  }

  return aspect;
}

}  // namespace bsrvcore::runtime
