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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <exception>
#include <ranges>
#include <stdexcept>
#include <string>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/bsrvrun/http_request_aspect_handler_factory.h"
#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/bsrvrun/service_factory.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "config_types.h"
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

#ifdef _WIN32
std::string GetLastErrorMessage(DWORD error_code);
#endif

void* ResolveFactorySymbol(void* handle, const std::string& library,
                           const char* symbol_name) {
  void* symbol = nullptr;
#ifdef _WIN32
  symbol = reinterpret_cast<void*>(
      GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol_name));
  if (symbol == nullptr) {
    throw std::runtime_error("`" + std::string(symbol_name) +
                             "` not found in `" + library +
                             "`: " + GetLastErrorMessage(GetLastError()));
  }
#else
  dlerror();
  symbol = dlsym(handle, symbol_name);
  const char* symbol_error = dlerror();
  if (symbol_error != nullptr || symbol == nullptr) {
    throw std::runtime_error("`" + std::string(symbol_name) +
                             "` not found in `" + library + "`");
  }
#endif
  return symbol;
}

std::exception_ptr DestroyServiceRecords(
    std::vector<service_runtime_detail::LoadedServiceRecord>* records,
    bsrvcore::HttpServer* server) {
  std::exception_ptr first_error;
  for (auto& record : std::ranges::reverse_view(*records)) {
    if (record.destroyed) {
      continue;
    }

    if (server != nullptr) {
      server->SetServiceProvider(record.slot, nullptr);
    }

    try {
      if (record.factory != nullptr && record.service != nullptr) {
        record.factory->DestroyService(record.service);
      }
    } catch (...) {
      if (first_error == nullptr) {
        first_error = std::current_exception();
      }
    }

    record.destroyed = true;
  }

  records->clear();
  return first_error;
}

#ifdef _WIN32
std::string GetLastErrorMessage(DWORD error_code) {
  LPSTR buffer = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD len =
      FormatMessageA(flags, nullptr, error_code, 0,
                     reinterpret_cast<LPSTR>(&buffer), 0, nullptr);

  if (len == 0 || buffer == nullptr) {
    return "error code " +
           std::to_string(static_cast<unsigned long>(error_code));
  }

  std::string message(buffer, len);
  LocalFree(buffer);
  while (!message.empty() &&
         (message.back() == '\r' || message.back() == '\n')) {
    message.pop_back();
  }
  return message.empty() ? "unknown" : message;
}
#endif

}  // namespace

PluginLoader::~PluginLoader() {
  (void)DestroyServiceRecords(&loaded_services_, nullptr);

  for (auto& it : std::ranges::reverse_view(handle_order_)) {
    auto handle_it = handles_.find(it);
    if (handle_it != handles_.end() && handle_it->second != nullptr) {
#ifdef _WIN32
      FreeLibrary(reinterpret_cast<HMODULE>(handle_it->second));
#else
      dlclose(handle_it->second);
#endif
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

  void* handle = nullptr;
#ifdef _WIN32
  handle = reinterpret_cast<void*>(LoadLibraryA(path.c_str()));
#else
  dlerror();
  handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
  if (handle == nullptr) {
#ifdef _WIN32
    const DWORD error_code = GetLastError();
    throw std::runtime_error("failed to load library `" + path +
                             "`: " + GetLastErrorMessage(error_code));
#else
    const char* error = dlerror();
    throw std::runtime_error("failed to load library `" + path +
                             "`: " + (error == nullptr ? "unknown" : error));
#endif
  }

  handles_.emplace(path, handle);
  handle_order_.emplace_back(path);
  return handle;
}

bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> PluginLoader::CreateHandler(
    const FactoryConfig& config) const {
  void* handle = GetOrOpenLibrary(config.library);
  void* symbol =
      ResolveFactorySymbol(handle, config.library, "GetHandlerFactory");

  auto fn = reinterpret_cast<bsrvcore::bsrvrun::GetHandlerFactoryFn>(symbol);
  bsrvcore::bsrvrun::HttpRequestHandlerFactory* factory = fn();
  if (factory == nullptr) {
    throw std::runtime_error("GetHandlerFactory returned nullptr in `" +
                             config.library + "`");
  }

  // Build a fresh ParameterMap per creation call so factories can treat it as
  // request-local configuration instead of shared mutable plugin state.
  RuntimeParameterMap params = BuildMap(config);
  auto handler = factory->Get(&params);
  if (!handler) {
    throw std::runtime_error("handler factory returned nullptr in `" +
                             config.library + "`");
  }

  return handler;
}

bsrvcore::OwnedPtr<bsrvcore::HttpRequestAspectHandler>
PluginLoader::CreateAspect(const FactoryConfig& config) const {
  void* handle = GetOrOpenLibrary(config.library);
  void* symbol =
      ResolveFactorySymbol(handle, config.library, "GetAspectFactory");

  auto fn = reinterpret_cast<bsrvcore::bsrvrun::GetAspectFactoryFn>(symbol);
  bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory* factory = fn();
  if (factory == nullptr) {
    throw std::runtime_error("GetAspectFactory returned nullptr in `" +
                             config.library + "`");
  }

  RuntimeParameterMap params = BuildMap(config);
  auto aspect = factory->Get(&params);
  if (!aspect) {
    throw std::runtime_error("aspect factory returned nullptr in `" +
                             config.library + "`");
  }

  return aspect;
}

void* PluginLoader::CreateService(const ServiceConfig& config) {
  void* handle = GetOrOpenLibrary(config.factory.library);
  void* symbol =
      ResolveFactorySymbol(handle, config.factory.library, "GetServiceFactory");

  auto fn = reinterpret_cast<bsrvcore::bsrvrun::GetServiceFactoryFn>(symbol);
  bsrvcore::bsrvrun::ServiceFactory* factory = fn();
  if (factory == nullptr) {
    throw std::runtime_error("GetServiceFactory returned nullptr in `" +
                             config.factory.library + "`");
  }

  RuntimeParameterMap params = BuildMap(config.factory);
  void* service = factory->GenerateService(&params);
  if (service == nullptr) {
    throw std::runtime_error("service factory returned nullptr in `" +
                             config.factory.library + "`");
  }

  loaded_services_.push_back(service_runtime_detail::LoadedServiceRecord{
      .slot = config.slot,
      .service = service,
      .factory = factory,
  });
  return service;
}

void PluginLoader::DestroyServices(bsrvcore::HttpServer* server) {
  if (auto error = DestroyServiceRecords(&loaded_services_, server);
      error != nullptr) {
    std::rethrow_exception(error);
  }
}

}  // namespace bsrvcore::runtime
