/**
 * @file example_logger_custom.cc
 * @brief Custom Logger implementation.
 *
 * Demonstrates:
 * - Implementing Logger
 * - Injecting logger with SetLogger
 *
 * Prerequisites: Boost, OpenSSL (required by bsrvcore build).
 * Build: cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=ON
 * Run: ./build/examples/example_logger_custom
 */

// BEGIN README_SNIPPET: logger_custom
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>
#include <string>

class ConsoleLogger : public bsrvcore::Logger {
 public:
  void Log(bsrvcore::LogLevel level, std::string message) override {
    std::clog << "[" << LevelToString(level) << "] " << message << std::endl;
  }

 private:
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
};

int main() {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  auto logger = std::make_shared<ConsoleLogger>();

  server
      ->SetLogger(logger)
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/log",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->Log(bsrvcore::LogLevel::kInfo, "Handling /log");
            task->GetResponse().result(boost::beast::http::status::ok);
            task->SetField(boost::beast::http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("Logged a message.\n");
          })
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8084});

  if (!server->Start(1)) {
    std::cerr << "Failed to start server." << std::endl;
    return 1;
  }

  logger->Log(bsrvcore::LogLevel::kInfo, "Listening on /log");
  std::cout << "Listening on http://0.0.0.0:8084/log" << std::endl;
  std::cout << "Press Enter to stop." << std::endl;
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: logger_custom
