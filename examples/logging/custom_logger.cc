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

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <iostream>
#include <memory>
#include <string>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/route/http_request_method.h"

class ConsoleLogger : public bsrvcore::Logger {
 public:
  void Log(bsrvcore::LogLevel level, std::string message) override {
    std::clog << "[" << LevelToString(level) << "] " << message << '\n';
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
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  auto logger = bsrvcore::AllocateShared<ConsoleLogger>();

  server->SetLogger(logger)
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/log",
          [](const std::shared_ptr<bsrvcore::HttpServerTask>& task) {
            task->Log(bsrvcore::LogLevel::kInfo, "Handling /log");
            task->GetResponse().result(boost::beast::http::status::ok);
            task->SetField(boost::beast::http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("Logged a message.\n");
          })
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8084}, 1);

  if (!server->Start()) {
    std::cerr << "Failed to start server." << '\n';
    return 1;
  }

  logger->Log(bsrvcore::LogLevel::kInfo, "Listening on /log");
  std::cout << "Listening on http://0.0.0.0:8084/log" << '\n';
  std::cout << "Press Enter to stop." << '\n';
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: logger_custom
