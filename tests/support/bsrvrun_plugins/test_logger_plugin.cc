#include <fstream>
#include <memory>
#include <mutex>
#include <string>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/bsrvrun/logger_factory.h"
#include "bsrvcore/bsrvrun/parameter_map.h"
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

class TestLogger final : public bsrvcore::Logger {
 public:
  TestLogger(std::string path, std::string prefix)
      : path_(std::move(path)), prefix_(std::move(prefix)) {}

  void Log(bsrvcore::LogLevel level, std::string message) override {
    if (path_.empty()) {
      return;
    }

    std::scoped_lock const lock(mutex_);
    std::ofstream out(path_, std::ios::app);
    out << prefix_ << LevelToString(level) << "|" << message << '\n';
  }

 private:
  std::mutex mutex_;
  std::string path_;
  std::string prefix_;
};

class TestLoggerFactory final : public bsrvcore::bsrvrun::LoggerFactory {
 public:
  [[nodiscard]] std::shared_ptr<bsrvcore::Logger> Create(
      bsrvcore::bsrvrun::ParameterMap* parameters) override {
    std::string path;
    std::string prefix = "logger|";
    if (parameters != nullptr) {
      path = parameters->Get(bsrvcore::bsrvrun::String("path")).ToStdString();
      const auto value =
          parameters->Get(bsrvcore::bsrvrun::String("prefix")).ToStdString();
      if (!value.empty()) {
        prefix = value;
      }
    }

    return bsrvcore::AllocateShared<TestLogger>(std::move(path),
                                                std::move(prefix));
  }
};

TestLoggerFactory g_factory;

}  // namespace

BSRVCORE_BSRVRUN_LOGGER_FACTORY_EXPORT GetLoggerFactory() { return &g_factory; }
