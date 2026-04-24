/**
 * @file demo_handler_plugin.cc
 * @brief Minimal bsrvrun handler plugin used by examples and tests.
 *
 * The exported factory returns a handler that appends a configured body suffix.
 * This keeps the plugin ABI example intentionally small and easy to copy.
 */

#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/logger.h"
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

bsrvcore::LogLevel ParseLogLevel(std::string level) {
  for (char& ch : level) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }

  if (level == "trace") {
    return bsrvcore::LogLevel::kTrace;
  }
  if (level == "debug") {
    return bsrvcore::LogLevel::kDebug;
  }
  if (level == "warn" || level == "warning") {
    return bsrvcore::LogLevel::kWarn;
  }
  if (level == "error") {
    return bsrvcore::LogLevel::kError;
  }
  if (level == "fatal") {
    return bsrvcore::LogLevel::kFatal;
  }
  return bsrvcore::LogLevel::kInfo;
}

struct HandlerLogConfig {
  bool enabled{false};
  bsrvcore::LogLevel level{bsrvcore::LogLevel::kInfo};
  std::string message;
};

HandlerLogConfig ParseLogConfig(bsrvcore::bsrvrun::ParameterMap* params) {
  HandlerLogConfig config;
  if (params == nullptr) {
    return config;
  }

  config.message =
      params->Get(bsrvcore::bsrvrun::String("log_message")).ToStdString();
  if (config.message.empty()) {
    return config;
  }

  const auto level =
      params->Get(bsrvcore::bsrvrun::String("log_level")).ToStdString();
  config.enabled = true;
  config.level = ParseLogLevel(level);
  return config;
}

class DemoHandler : public bsrvcore::HttpRequestHandler {
 public:
  DemoHandler(std::string body, std::optional<std::size_t> service_slot,
              HandlerLogConfig log_config)
      : body_(std::move(body)),
        service_slot_(service_slot),
        log_config_(std::move(log_config)) {}

  // Service method accepts const-ref to HttpServerTask (v0.16.0+).
  void Service(const std::shared_ptr<bsrvcore::HttpServerTask>& task) override {
    if (log_config_.enabled) {
      task->Log(log_config_.level, log_config_.message);
    }
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
  HandlerLogConfig log_config_;
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
    return bsrvcore::AllocateUnique<DemoHandler>(
        body, ParseOptionalSlot(params), ParseLogConfig(params));
  }
};

DemoHandlerFactory g_factory;

}  // namespace

BSRVCORE_BSRVRUN_HANDLER_FACTORY_EXPORT GetHandlerFactory() {
  // bsrvrun resolves this exact symbol name at runtime.
  return &g_factory;
}
