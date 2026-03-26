/**
 * @file multipart_parser.h
 * @brief Multipart/form-data request body wrapper.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-25
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_SERVER_MULTIPART_PARSER_H_
#define BSRVCORE_CONNECTION_SERVER_MULTIPART_PARSER_H_

#include <boost/asio/any_io_executor.hpp>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bsrvcore/connection/server/http_server_task.h"

namespace bsrvcore {

/**
 * @brief Parses an already-buffered multipart/form-data request body.
 *
 * The parser operates on the current request body snapshot. When constructed
 * from a task, async dump callbacks are dispatched on that task's worker
 * executor. When constructed from a request alone, callback-based async dump
 * helpers require a valid executor argument in the constructor. The overloads
 * that ignore completion can run without a caller-provided executor.
 */
class MultipartParser {
 public:
  using DumpCallback = std::function<void(bool)>;

  explicit MultipartParser(HttpTaskBase& task);
  explicit MultipartParser(
      const HttpRequest& request,
      boost::asio::any_io_executor executor = boost::asio::any_io_executor());

  /**
   * @brief Get the number of parsed parts.
   * @return Part count, or zero if the request is not valid multipart data.
   */
  std::size_t GetPartCount() const noexcept;

  /**
   * @brief Get the part content type header.
   * @param part_idx Part index.
   * @return Content type value, or empty view when absent/invalid.
   */
  std::string_view GetPartType(std::size_t part_idx) const noexcept;

  /**
   * @brief Check whether a part looks like a file upload.
   * @param part_idx Part index.
   * @return true if the part has a filename parameter in content-disposition.
   */
  bool IsFile(std::size_t part_idx) const noexcept;

  /**
   * @brief Dump one multipart file part to disk asynchronously.
   * @param part_idx Part index.
   * @param path Destination path.
   * @param callback Completion callback receiving final write success/failure.
   * @return true if the part is dumpable and work was scheduled.
   */
  bool AsyncDumpToDisk(std::size_t part_idx, std::filesystem::path path,
                       DumpCallback callback) const;

  /**
   * @brief Dump one multipart file part to disk asynchronously and ignore the
   *        final completion result.
   * @param part_idx Part index.
   * @param path Destination path.
   * @return true if the part is dumpable and work was scheduled.
   */
  bool AsyncDumpToDisk(std::size_t part_idx, std::filesystem::path path) const {
    return AsyncDumpToDisk(part_idx, std::move(path), DumpCallback{});
  }

 private:
  struct PartData {
    std::string content_type;
    std::string payload;
    bool is_file{false};
  };

  void Parse(const HttpRequest& request);

  std::vector<PartData> parts_;
  boost::asio::any_io_executor executor_;
};

}  // namespace bsrvcore

#endif
