/**
 * @file logger.h
 * @brief Logging interface and log level definitions
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-24
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines the logging interface and log levels for the bsrvcore framework.
 * Provides a simple abstraction for logging implementations with six
 * standard log levels from Trace to Fatal.
 */

#pragma once

#ifndef BSRVCORE_LOGGER_H_
#define BSRVCORE_LOGGER_H_

#include <cstdint>
#include <string>

namespace bsrvcore {

/**
 * @brief Log levels for categorizing log messages by severity
 *
 * Standard log levels following common logging practices. Each level
 * represents a different severity and intended audience for log messages.
 *
 * @code
 * // Example usage in application code
 * logger->Log(LogLevel::kInfo, "Server started on port 8080");
 * logger->Log(LogLevel::kWarn, "High memory usage detected");
 * logger->Log(LogLevel::kError, "Failed to connect to database");
 *
 * // Example usage in HTTP request handler
 * void Service(std::shared_ptr<HttpServerTask> task) override {
 *   task->Log(LogLevel::kDebug, "Processing request: " +
 * task->GetRequest().target());
 *
 *   try {
 *     // Process request
 *     task->Log(LogLevel::kInfo, "Request processed successfully");
 *   } catch (const std::exception& e) {
 *     task->Log(LogLevel::kError, std::string("Request failed: ") + e.what());
 *   }
 * }
 * @endcode
 */
enum class LogLevel : std::uint8_t {
  kTrace = 0,  ///< Detailed debugging information (most verbose)
  kDebug,      ///< Debugging information useful during development
  kInfo,       ///< General operational information about system state
  kWarn,       ///< Warning messages indicating potential issues
  kError,      ///< Error messages indicating operation failures
  kFatal       ///< Fatal errors causing application termination (least verbose)
};

/**
 * @brief Abstract interface for logging implementations
 *
 * Logger defines the contract for all logging implementations in bsrvcore.
 * Concrete implementations can provide logging to console, files, network,
 * or other destinations while maintaining a consistent interface.
 *
 * @note The destructor is deleted to prevent direct instantiation of the
 *       abstract base class. Derived classes must implement their own
 *       destruction logic.
 *
 * @code
 * // Example custom logger implementation
 * class FileLogger : public Logger {
 * public:
 *   FileLogger(const std::string& filename) : file_(filename) {}
 *
 *   void Log(LogLevel level, std::string_view message) override {
 *     auto timestamp = std::chrono::system_clock::now();
 *     file_ << "[" << timestamp << "] [" << LevelToString(level) << "] "
 *           << message << std::endl;
 *   }
 *
 *   ~FileLogger() override {
 *     file_.close();
 *   }
 *
 * private:
 *   std::ofstream file_;
 *
 *   std::string LevelToString(LogLevel level) {
 *     switch (level) {
 *       case LogLevel::kTrace: return "TRACE";
 *       case LogLevel::kDebug: return "DEBUG";
 *       case LogLevel::kInfo: return "INFO";
 *       case LogLevel::kWarn: return "WARN";
 *       case LogLevel::kError: return "ERROR";
 *       case LogLevel::kFatal: return "FATAL";
 *       default: return "UNKNOWN";
 *     }
 *   }
 * };
 *
 * // Usage in application
 * auto logger = std::make_unique<FileLogger>("server.log");
 * server->SetLogger(std::move(logger));
 * @endcode
 */
class Logger {
 public:
  /**
   * @brief Log a message with specified severity level
   * @param level Severity level of the log message
   * @param message Log message content
   */
  virtual void Log(LogLevel level, std::string message) = 0;

  /**
   * @brief Destructor prevents direct instantiation
   */
  virtual ~Logger() = default;
};

}  // namespace bsrvcore

#endif
