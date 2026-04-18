#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "bsrvcore/bsrvrun/http_request_handler_factory.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/route/http_request_handler.h"
#include "test_service_api.h"

namespace {

std::optional<std::size_t> ParseOptionalSlot(
    bsrvcore::bsrvrun::ParameterMap* parameters) {
  if (parameters == nullptr) {
    return std::nullopt;
  }

  const auto slot_value =
      parameters->Get(bsrvcore::bsrvrun::String("service_slot")).ToStdString();
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
  std::transform(level.begin(), level.end(), level.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

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

HandlerLogConfig ParseLogConfig(
    bsrvcore::bsrvrun::ParameterMap* parameters) {
  HandlerLogConfig config;
  if (parameters == nullptr) {
    return config;
  }

  config.message =
      parameters->Get(bsrvcore::bsrvrun::String("log_message")).ToStdString();
  if (config.message.empty()) {
    return config;
  }

  config.enabled = true;
  const auto level =
      parameters->Get(bsrvcore::bsrvrun::String("log_level")).ToStdString();
  config.level = ParseLogLevel(level);
  return config;
}

class TestHandler : public bsrvcore::HttpRequestHandler {
 public:
  TestHandler(std::string body, bool append_thread_id,
              std::optional<std::size_t> service_slot,
              HandlerLogConfig log_config)
      : body_(std::move(body)),
        append_thread_id_(append_thread_id),
        service_slot_(service_slot),
        log_config_(std::move(log_config)) {}

  void Service(const std::shared_ptr<bsrvcore::HttpServerTask>& task) override {
    if (log_config_.enabled) {
      task->Log(log_config_.level, log_config_.message);
    }
    if (service_slot_.has_value()) {
      if (auto* service = task->GetService<TestServiceData>(*service_slot_)) {
        task->AppendBody(service->body);
      }
    }
    task->AppendBody(body_);
    if (append_thread_id_) {
      std::ostringstream oss;
      oss << std::this_thread::get_id();
      task->AppendBody(oss.str());
    }
  }

 private:
  std::string body_;
  bool append_thread_id_;
  std::optional<std::size_t> service_slot_;
  HandlerLogConfig log_config_;
};

class TestHandlerFactory : public bsrvcore::bsrvrun::HttpRequestHandlerFactory {
 public:
  bsrvcore::OwnedPtr<bsrvcore::HttpRequestHandler> Get(
      bsrvcore::bsrvrun::ParameterMap* parameters) override {
    std::string body = "handler|";
    bool append_thread_id = false;
    if (parameters != nullptr) {
      const auto value =
          parameters->Get(bsrvcore::bsrvrun::String("body")).ToStdString();
      if (!value.empty()) {
        body = value;
      }
      const auto thread_id =
          parameters->Get(bsrvcore::bsrvrun::String("thread_id")).ToStdString();
      append_thread_id =
          (thread_id == "1" || thread_id == "true" || thread_id == "TRUE");
    }

    return bsrvcore::AllocateUnique<TestHandler>(
        body, append_thread_id, ParseOptionalSlot(parameters),
        ParseLogConfig(parameters));
  }
};

TestHandlerFactory g_factory;

}  // namespace

BSRVCORE_BSRVRUN_HANDLER_FACTORY_EXPORT GetHandlerFactory() {
  return &g_factory;
}
