/**
 * @file put_processor.h
 * @brief PUT request body wrapper.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-25
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_SERVER_PUT_PROCESSOR_H_
#define BSRVCORE_CONNECTION_SERVER_PUT_PROCESSOR_H_

#include <boost/asio/any_io_executor.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/file/file_writer.h"

namespace bsrvcore {

/**
 * @brief Lightweight wrapper for PUT request payloads exposed as FileWriter.
 */
class PutProcessor : public std::enable_shared_from_this<PutProcessor>,
                     public NonCopyableNonMovable<PutProcessor> {
 public:
  /** @brief Completion callback used by the compatibility dump API. */
  using DumpCallback = std::function<void(bool)>;

  /** @brief Create a processor from a server task. */
  [[nodiscard]] static std::shared_ptr<PutProcessor> Create(HttpTaskBase& task);
  /** @brief Create a processor from a request and explicit executors. */
  [[nodiscard]] static std::shared_ptr<PutProcessor> Create(
      const HttpRequest& request, IoExecutor work_executor,
      IoExecutor callback_executor);
  /** @brief Create a processor using the same executor for work and callbacks.
   */
  [[nodiscard]] static std::shared_ptr<PutProcessor> Create(
      const HttpRequest& request, IoExecutor executor = IoExecutor());

  /** @brief Return the writer that owns the request payload. */
  [[nodiscard]] std::shared_ptr<FileWriter> GetFileWriter() const noexcept;

  /** @brief Compatibility helper that writes the payload to disk. */
  [[nodiscard]] bool AsyncDumpToDisk(std::filesystem::path path,
                                     DumpCallback callback) const;
  /** @brief Compatibility helper that writes the payload to disk without
   * callback. */
  [[nodiscard]] bool AsyncDumpToDisk(std::filesystem::path path) const {
    return AsyncDumpToDisk(std::move(path), DumpCallback{});
  }

 private:
  struct PrivateTag {};

  PutProcessor(PrivateTag, const HttpRequest& request, IoExecutor work_executor,
               IoExecutor callback_executor);

  std::shared_ptr<FileWriter> writer_;
  IoExecutor work_executor_;
  IoExecutor callback_executor_;
  bool is_put_{false};
};

}  // namespace bsrvcore

#endif
