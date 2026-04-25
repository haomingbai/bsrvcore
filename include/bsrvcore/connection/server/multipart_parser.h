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
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/file/file_writer.h"

namespace bsrvcore {

/**
 * @brief Wrapper around multipart/form-data request bodies with file accessors.
 */
class MultipartParser : public std::enable_shared_from_this<MultipartParser>,
                        public NonCopyableNonMovable<MultipartParser> {
 public:
  /** @brief Completion callback used by the compatibility dump API. */
  using DumpCallback = std::function<void(bool)>;

  /** @brief Create a parser from a server task. */
  [[nodiscard]] static std::shared_ptr<MultipartParser> Create(
      HttpTaskBase& task);
  /** @brief Create a parser from a request and explicit executors. */
  [[nodiscard]] static std::shared_ptr<MultipartParser> Create(
      const HttpRequest& request, IoExecutor work_executor,
      IoExecutor callback_executor);
  /** @brief Create a parser using the same executor for work and callbacks. */
  [[nodiscard]] static std::shared_ptr<MultipartParser> Create(
      const HttpRequest& request, IoExecutor executor = IoExecutor());

  /** @brief Return the number of parsed multipart sections. */
  [[nodiscard]] std::size_t GetPartCount() const noexcept;
  /** @brief Return the declared content type of one part. */
  [[nodiscard]] std::string_view GetPartType(
      std::size_t part_idx) const noexcept;
  /** @brief Whether the selected part is file-backed. */
  [[nodiscard]] bool IsFile(std::size_t part_idx) const noexcept;
  /** @brief Return the writer that owns one parsed file part. */
  [[nodiscard]] std::shared_ptr<FileWriter> GetFileWriter(
      std::size_t part_idx) const noexcept;

  /** @brief Compatibility helper that writes one file part to disk. */
  [[nodiscard]] bool AsyncDumpToDisk(std::size_t part_idx,
                                     std::filesystem::path path,
                                     DumpCallback callback) const;
  /** @brief Compatibility helper that writes one file part to disk without
   * callback. */
  [[nodiscard]] bool AsyncDumpToDisk(std::size_t part_idx,
                                     std::filesystem::path path) const {
    return AsyncDumpToDisk(part_idx, std::move(path), DumpCallback{});
  }

 private:
  struct PrivateTag {};

  struct PartData {
    std::string content_type;
    std::string payload;
    bool is_file{false};
    std::shared_ptr<FileWriter> writer;
  };
  using PartStorage = AllocatedVector<PartData>;

  MultipartParser(PrivateTag, const HttpRequest& request,
                  IoExecutor work_executor, IoExecutor callback_executor);

  void Parse(const HttpRequest& request);

  PartStorage parts_;
  IoExecutor work_executor_;
  IoExecutor callback_executor_;
};

}  // namespace bsrvcore

#endif
