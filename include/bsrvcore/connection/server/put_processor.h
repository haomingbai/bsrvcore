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
#include <utility>

#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"
#include "bsrvcore/file/file_writer.h"

namespace bsrvcore {
class HttpTaskBase;

/**
 * @brief Lightweight wrapper for PUT request payloads exposed as FileWriter.
 */
class PutProcessor : public std::enable_shared_from_this<PutProcessor>,
                     public NonCopyableNonMovable<PutProcessor> {
 public:
  /** @brief Completion callback used by the compatibility dump API. */
  using DumpCallback = std::function<void(bool)>;

  /**
   * @brief Create a processor from a server task.
   *
   * @param task Server task whose PUT body should be wrapped.
   * @return PUT processor instance.
   */
  [[nodiscard]] static std::shared_ptr<PutProcessor> Create(HttpTaskBase& task);
  /**
   * @brief Create a processor from a request and explicit executors.
   *
   * @param request HTTP request containing the PUT payload.
   * @param work_executor Executor used for file dump work.
   * @param callback_executor Executor used to deliver dump callbacks.
   * @return PUT processor instance.
   */
  [[nodiscard]] static std::shared_ptr<PutProcessor> Create(
      const HttpRequest& request, IoExecutor work_executor,
      IoExecutor callback_executor);
  /**
   * @brief Create a processor using the same executor for work and callbacks.
   *
   * @param request HTTP request containing the PUT payload.
   * @param executor Executor used for work and callbacks.
   * @return PUT processor instance.
   */
  [[nodiscard]] static std::shared_ptr<PutProcessor> Create(
      const HttpRequest& request, IoExecutor executor = IoExecutor());

  /**
   * @brief Return the writer that owns the request payload.
   *
   * @return File writer containing the request payload.
   */
  [[nodiscard]] std::shared_ptr<FileWriter> GetFileWriter() const noexcept;

  /**
   * @brief Compatibility helper that writes the payload to disk.
   *
   * @param path Destination file path.
   * @param callback Completion callback receiving success status.
   * @return True when asynchronous dumping was started.
   */
  [[nodiscard]] bool AsyncDumpToDisk(std::filesystem::path path,
                                     DumpCallback callback) const;
  /**
   * @brief Compatibility helper that writes the payload to disk without
   * callback.
   *
   * @param path Destination file path.
   * @return True when asynchronous dumping was started.
   */
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
