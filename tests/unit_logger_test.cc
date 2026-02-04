#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "bsrvcore/http_server.h"
#include "bsrvcore/logger.h"

namespace {

// Mock logger used to verify Log calls.
class MockLogger : public bsrvcore::Logger {
 public:
  MOCK_METHOD(void, Log, (bsrvcore::LogLevel, std::string), (override));
};

}  // namespace

// Verify server forwards Log calls to the configured logger.
TEST(LoggerTest, SetLoggerAndLog) {
  bsrvcore::HttpServer server(1);
  auto logger = std::make_shared<MockLogger>();

  server.SetLogger(logger);

  EXPECT_CALL(*logger, Log(bsrvcore::LogLevel::kInfo, "hello"));
  server.Log(bsrvcore::LogLevel::kInfo, "hello");
}
