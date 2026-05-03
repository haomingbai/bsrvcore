#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/logger.h"

namespace {

class RecordingLogger : public bsrvcore::Logger {
 public:
  void Log(bsrvcore::LogLevel level, std::string message) override {
    last_entry = Entry{level, std::move(message)};
  }

  struct Entry {
    bsrvcore::LogLevel level;
    std::string message;
  };

  std::optional<Entry> last_entry;
};

}  // namespace

// Verify server forwards Log calls to the configured logger.
TEST(LoggerTest, SetLoggerAndLog) {
  bsrvcore::HttpServer server(1);
  auto logger = std::make_shared<RecordingLogger>();

  server.SetLogger(logger);

  server.Log(bsrvcore::LogLevel::kInfo, "hello");

  ASSERT_TRUE(logger->last_entry.has_value());
  EXPECT_EQ(logger->last_entry->level, bsrvcore::LogLevel::kInfo);
  EXPECT_EQ(logger->last_entry->message, "hello");
}
