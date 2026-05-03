/**
 * @file demo_logger_plugin.cc
 * @brief Minimal bsrvrun logger plugin used by examples.
 */

#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "bsrvcore/bsrvrun/logger_factory.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
#include "bsrvcore/bsrvrun/plugin_export.h"
#include "bsrvcore/bsrvrun/string.h"
#include "bsrvcore/core/logger.h"

namespace {

const char* LevelToString(bsrvcore::LogLevel level) {
  switch (level) {
    case bsrvcore::LogLevel::kTrace:
      return "TRACE";
    case bsrvcore::LogLevel::kDebug:
      return "DEBUG";
    case bsrvcore::LogLevel::kInfo:
      return "INFO";
    case bsrvcore::LogLevel::kWarn:
      return "WARN";
    case bsrvcore::LogLevel::kError:
      return "ERROR";
    case bsrvcore::LogLevel::kFatal:
      return "FATAL";
    default:
      return "UNKNOWN";
  }
}

class DemoLogger final : public bsrvcore::Logger {
 public:
  explicit DemoLogger(std::string prefix) : prefix_(std::move(prefix)) {}

  void Log(bsrvcore::LogLevel level, std::string message) override {
    std::scoped_lock const lock(mutex_);
    std::clog << prefix_ << LevelToString(level) << "|" << message << '\n';
  }

 private:
  std::mutex mutex_;
  std::string prefix_;
};

class DemoLoggerFactory final : public bsrvcore::bsrvrun::LoggerFactory {
 public:
  [[nodiscard]] std::shared_ptr<bsrvcore::Logger> Create(
      bsrvcore::bsrvrun::ParameterMap* params) override {
    std::string prefix = "demo-logger|";
    if (params != nullptr) {
      const auto value =
          params->Get(bsrvcore::bsrvrun::String("prefix")).ToStdString();
      if (!value.empty()) {
        prefix = value;
      }
    }
    return std::make_shared<DemoLogger>(std::move(prefix));
  }
};

DemoLoggerFactory g_factory;

}  // namespace

BSRVCORE_BSRVRUN_LOGGER_FACTORY_EXPORT GetLoggerFactory() { return &g_factory; }
