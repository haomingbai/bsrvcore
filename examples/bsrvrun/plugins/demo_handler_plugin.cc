/**
 * @file demo_handler_plugin.cc
 * @brief Minimal bsrvrun handler plugin used by examples and tests.
 *
 * The exported factory returns a handler that appends a configured body suffix.
 * This keeps the plugin ABI example intentionally small and easy to copy.
 */

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/route/http_request_handler.h"
#include "demo_service_api.h"

namespace {

std::optional<std::size_t> ParseOptionalSlot(
    bsrvcore::bsrvrun::ParameterMap* params) {
  if (params == nullptr) {
    return std::nullopt;
  }

  const auto slot_value =
      params->Get(bsrvcore::bsrvrun::String("service_slot")).ToStdString();
  if (slot_value.empty()) {
    return std::nullopt;
  }

  try {
    return static_cast<std::size_t>(std::stoull(slot_value));
  } catch (...) {
    return std::nullopt;
  }
}

class DemoHandler : public bsrvcore::HttpRequestHandler {
 public:
  DemoHandler(std::string body, std::optional<std::size_t> service_slot)
      : body_(std::move(body)), service_slot_(service_slot) {}

  // Service method accepts const-ref to HttpServerTask (v0.16.0+).
  void Service(const std::shared_ptr<bsrvcore::HttpServerTask>& task) override {
    if (service_slot_.has_value()) {
      if (auto* service = task->GetService<DemoServiceData>(*service_slot_)) {
        task->AppendBody(service->prefix);
      }
    }
    task->AppendBody(body_);
  }

 private:
  std::string body_;
  std::optional<std::size_t> service_slot_;
};

class DemoHandlerFactory : public bsrvcore::bsrvrun::HttpRequestHandlerFactory {
 public:
  bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> Get(
      bsrvcore::bsrvrun::ParameterMap* params) override {
    std::string body = "demo-handler|";
    if (params != nullptr) {
      const auto body_value =
          params->Get(bsrvcore::bsrvrun::String("body")).ToStdString();
      if (!body_value.empty()) {
        body = body_value;
      }
    }
    return bsrvcore::AllocateUnique<DemoHandler>(body,
                                                 ParseOptionalSlot(params));
  }
};

DemoHandlerFactory g_factory;

}  // namespace

BSRVCORE_BSRVRUN_HANDLER_FACTORY_EXPORT GetHandlerFactory() {
  // bsrvrun resolves this exact symbol name at runtime.
  return &g_factory;
}
