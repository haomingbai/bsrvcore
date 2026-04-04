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

  /** @brief Create a writer with separate work and callback executors. */
  [[nodiscard]] static std::shared_ptr<FileWriter> Create(
      std::string_view payload, boost::asio::any_io_executor work_executor,
      boost::asio::any_io_executor callback_executor);
  /** @brief Create a writer using the same executor for work and callbacks. */
  [[nodiscard]] static std::shared_ptr<FileWriter> Create(
      std::string_view payload, boost::asio::any_io_executor executor = {});

  /** @brief Destroy the writer and release its owned buffer. */
  ~FileWriter();

  /** @brief Whether the writer holds a valid payload buffer. */
  [[nodiscard]] bool IsValid() const noexcept;
  /** @brief Return the payload size in bytes. */
  [[nodiscard]] std::size_t Size() const noexcept;
  /** @brief Return a pointer to the internal payload bytes. */
  [[nodiscard]] const char* Data() const noexcept;
  /** @brief Copy the payload into a caller-provided buffer. */
  [[nodiscard]] bool CopyTo(void* dest, std::size_t dest_size) const noexcept;

  /** @brief Start an asynchronous disk write into a caller-supplied state. */
  [[nodiscard]] bool AsyncWriteToDisk(std::filesystem::path path,
                                      std::shared_ptr<FileWritingState> state,
                                      Callback callback = {}) const;
  /** @brief Start an asynchronous disk write using an internally created state.
   */
  [[nodiscard]] bool AsyncWriteToDisk(std::filesystem::path path,
                                      Callback callback = {}) const;

  /** @brief Return the executor used for disk work dispatch. */
  [[nodiscard]] boost::asio::any_io_executor GetWorkExecutor() const noexcept;
  /** @brief Return the executor used for completion callbacks. */
  [[nodiscard]] boost::asio::any_io_executor GetCallbackExecutor()
      const noexcept;

 private:
  struct PrivateTag {};

  FileWriter(PrivateTag, std::string_view payload,
             boost::asio::any_io_executor work_executor,
             boost::asio::any_io_executor callback_executor);

  void Assign(std::string_view payload);

  char* data_{nullptr};
  std::size_t size_{0};
  bool valid_{false};
  boost::asio::any_io_executor work_executor_;
  boost::asio::any_io_executor callback_executor_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_FILE_FILE_WRITER_H_
