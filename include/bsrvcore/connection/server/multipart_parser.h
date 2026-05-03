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
#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"
#include "bsrvcore/file/file_writer.h"

namespace bsrvcore {
class HttpTaskBase;

/**
 * @brief Wrapper around multipart/form-data request bodies with file accessors.
 */
class MultipartParser : public std::enable_shared_from_this<MultipartParser>,
                        public NonCopyableNonMovable<MultipartParser> {
 public:
  /** @brief Completion callback used by the compatibility dump API. */
  using DumpCallback = std::function<void(bool)>;

  /**
   * @brief Create a parser from a server task.
   *
   * @param task Server task whose request body should be parsed.
   * @return Multipart parser instance.
   */
  [[nodiscard]] static std::shared_ptr<MultipartParser> Create(
      HttpTaskBase& task);
  /**
   * @brief Create a parser from a request and explicit executors.
   *
   * @param request HTTP request containing multipart/form-data.
   * @param work_executor Executor used for file dump work.
   * @param callback_executor Executor used to deliver dump callbacks.
   * @return Multipart parser instance.
   */
  [[nodiscard]] static std::shared_ptr<MultipartParser> Create(
      const HttpRequest& request, IoExecutor work_executor,
      IoExecutor callback_executor);
  /**
   * @brief Create a parser using the same executor for work and callbacks.
   *
   * @param request HTTP request containing multipart/form-data.
   * @param executor Executor used for work and callbacks.
   * @return Multipart parser instance.
   */
  [[nodiscard]] static std::shared_ptr<MultipartParser> Create(
      const HttpRequest& request, IoExecutor executor = IoExecutor());

  /**
   * @brief Return the number of parsed multipart sections.
   *
   * @return Parsed part count.
   */
  [[nodiscard]] std::size_t GetPartCount() const noexcept;
  /**
   * @brief Return the declared content type of one part.
   *
   * @param part_idx Zero-based part index.
   * @return Part Content-Type, or empty string view for invalid index.
   */
  [[nodiscard]] std::string_view GetPartType(
      std::size_t part_idx) const noexcept;
  /**
   * @brief Whether the selected part is file-backed.
   *
   * @param part_idx Zero-based part index.
   * @return True when the part is file-backed.
   */
  [[nodiscard]] bool IsFile(std::size_t part_idx) const noexcept;
  /**
   * @brief Return the writer that owns one parsed file part.
   *
   * @param part_idx Zero-based part index.
   * @return File writer for the part, or null when unavailable.
   */
  [[nodiscard]] std::shared_ptr<FileWriter> GetFileWriter(
      std::size_t part_idx) const noexcept;

  /**
   * @brief Compatibility helper that writes one file part to disk.
   *
   * @param part_idx Zero-based file part index.
   * @param path Destination file path.
   * @param callback Completion callback receiving success status.
   * @return True when asynchronous dumping was started.
   */
  [[nodiscard]] bool AsyncDumpToDisk(std::size_t part_idx,
                                     std::filesystem::path path,
                                     DumpCallback callback) const;
  /**
   * @brief Compatibility helper that writes one file part to disk without
   * callback.
   *
   * @param part_idx Zero-based file part index.
   * @param path Destination file path.
   * @return True when asynchronous dumping was started.
   */
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
