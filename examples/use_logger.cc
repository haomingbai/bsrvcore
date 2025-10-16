/**
 * @file use_logger.cc
 * @brief Set a logger to write down logs.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-16
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details Log is an important tool in developing reliable apps and find
 * potential bugs in programming. For some beginners who are not familiar
 * with GDB or some kind of debuggers, a piece of easy-to-read and insightful
 * log can greately help the programmer to locate errors.
 * Therefore, here we are going to show how to set up a logger with this
 * framework, which is crucial for robust networking system.
 */

#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <cstdio>
#include <format>
#include <iostream>
#include <memory>
#include <string>

#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "bsrvcore/logger.h"

// In order to minimize dependences, here we use clog to demonstrate
// the usage of the Logger.
// However, in real programming, according the the document of SPDLOG,
// you should NEVER use std::clog or std::cout to output loggs.
class MyLogger : public bsrvcore::Logger {
 public:
  void Log(bsrvcore::LogLevel level, std::string message) override {
    std::clog << std::format("[{} {}]: {}\n", GetCurrentTime(),
                             LevelToString(level), message);
  }

 private:
  std::string LevelToString(bsrvcore::LogLevel level) {
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

  std::string GetCurrentTime() {
    auto now = std::chrono::system_clock::now();
    std::string time_str = std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
    return time_str;
  }
};

int main() {
  // Setup a simple server.
  auto server = std::make_unique<bsrvcore::HttpServer>(10);
  auto my_logger = std::make_shared<MyLogger>();
  server
      // When processing with the request, we can make a log through the logger.
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/use_logger/get",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->Log(bsrvcore::LogLevel::kInfo,
                      "A Get request has been received");
            task->SetBody(
                "<!DOCTYPE html><body>Your request has been received.</body>");
          })
      // Set a logger for the server.
      ->SetLogger(my_logger);
  server->AddListen({boost::asio::ip::make_address("0.0.0.0"), 2025});

  if (server->Start(2)) {
    // At this time, with shared_ptr used, if your logger is thread-safe,
    // the logger is still valid.
    my_logger->Log(bsrvcore::LogLevel::kInfo,
                   "The server has started successfully!\n");
    std::getchar();
    my_logger->Log(bsrvcore::LogLevel::kInfo, "The server is stopping.\n");
  } else {
    my_logger->Log(bsrvcore::LogLevel::kError, "The server fails to start.\n");
  }
}
