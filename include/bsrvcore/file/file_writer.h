/**
 * @file file_writer.h
 * @brief In-memory file payload with async disk write helpers.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_FILE_FILE_WRITER_H_
#define BSRVCORE_FILE_FILE_WRITER_H_

#include <boost/asio/any_io_executor.hpp>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>

#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"
#include "bsrvcore/file/file_state.h"

namespace bsrvcore {

/**
 * @brief Owns an in-memory file payload that can be written to disk.
 */
class FileWriter : public std::enable_shared_from_this<FileWriter>,
                   public NonCopyableNonMovable<FileWriter> {
 public:
  /** @brief Completion callback for async write operations. */
  using Callback = std::function<void(std::shared_ptr<FileWritingState>)>;

  /**
   * @brief Create a writer with separate work and callback executors.
   *
   * @param payload Bytes to own in memory.
   * @param work_executor Executor used for disk work.
   * @param callback_executor Executor used to deliver callbacks.
   * @return File writer instance.
   */
  [[nodiscard]] static std::shared_ptr<FileWriter> Create(
      std::string_view payload, IoExecutor work_executor,
      IoExecutor callback_executor);
  /**
   * @brief Create a writer using the same executor for work and callbacks.
   *
   * @param payload Bytes to own in memory.
   * @param executor Executor used for work and callbacks.
   * @return File writer instance.
   */
  [[nodiscard]] static std::shared_ptr<FileWriter> Create(
      std::string_view payload, IoExecutor executor = {});

  /** @brief Destroy the writer and release its owned buffer. */
  ~FileWriter();

  /**
   * @brief Whether the writer holds a valid payload buffer.
   *
   * @return True when the writer has a usable payload buffer.
   */
  [[nodiscard]] bool IsValid() const noexcept;
  /**
   * @brief Return the payload size in bytes.
   *
   * @return Payload size in bytes.
   */
  [[nodiscard]] std::size_t Size() const noexcept;
  /**
   * @brief Return a pointer to the internal payload bytes.
   *
   * @return Pointer to the owned payload bytes.
   */
  [[nodiscard]] const char* Data() const noexcept;
  /**
   * @brief Copy the payload into a caller-provided buffer.
   *
   * @param dest Destination buffer.
   * @param dest_size Destination buffer size in bytes.
   * @return True when the payload fit in the destination buffer.
   */
  [[nodiscard]] bool CopyTo(void* dest, std::size_t dest_size) const noexcept;

  /**
   * @brief Start an asynchronous disk write into a caller-supplied state.
   *
   * @param path Destination file path.
   * @param state State object populated by the write.
   * @param callback Completion callback.
   * @return True when asynchronous writing was started.
   */
  [[nodiscard]] bool AsyncWriteToDisk(std::filesystem::path path,
                                      std::shared_ptr<FileWritingState> state,
                                      Callback callback = {}) const;
  /**
   * @brief Start an asynchronous disk write using an internally created state.
   *
   * @param path Destination file path.
   * @param callback Completion callback.
   * @return True when asynchronous writing was started.
   */
  [[nodiscard]] bool AsyncWriteToDisk(std::filesystem::path path,
                                      Callback callback = {}) const;

  /**
   * @brief Return the executor used for disk work dispatch.
   *
   * @return Work executor.
   */
  [[nodiscard]] IoExecutor GetWorkExecutor() const noexcept;
  /**
   * @brief Return the executor used for completion callbacks.
   *
   * @return Callback executor.
   */
  [[nodiscard]] IoExecutor GetCallbackExecutor() const noexcept;

 private:
  struct PrivateTag {};

  FileWriter(PrivateTag, std::string_view payload, IoExecutor work_executor,
             IoExecutor callback_executor);

  void Assign(std::string_view payload);

  char* data_{nullptr};
  std::size_t size_{0};
  bool valid_{false};
  IoExecutor work_executor_;
  IoExecutor callback_executor_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_FILE_FILE_WRITER_H_
