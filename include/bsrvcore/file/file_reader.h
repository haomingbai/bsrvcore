/**
 * @file file_reader.h
 * @brief Disk-backed file descriptor with async read helpers.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_FILE_FILE_READER_H_
#define BSRVCORE_FILE_FILE_READER_H_

#include <boost/asio/any_io_executor.hpp>
#include <filesystem>
#include <functional>
#include <memory>

#include "bsrvcore/core/trait.h"
#include "bsrvcore/file/file_state.h"

namespace bsrvcore {

/**
 * @brief Represents a file on disk that can be read asynchronously.
 */
class FileReader : public std::enable_shared_from_this<FileReader>,
                   public NonCopyableNonMovable<FileReader> {
 public:
  /** @brief Completion callback for async read operations. */
  using Callback = std::function<void(std::shared_ptr<FileReadingState>)>;

  /** @brief Create a reader with separate work and callback executors. */
  [[nodiscard]] static std::shared_ptr<FileReader> Create(
      std::filesystem::path path, boost::asio::any_io_executor work_executor,
      boost::asio::any_io_executor callback_executor);
  /** @brief Create a reader using the same executor for work and callbacks. */
  [[nodiscard]] static std::shared_ptr<FileReader> Create(
      std::filesystem::path path, boost::asio::any_io_executor executor = {});

  /** @brief Destroy the reader. */
  ~FileReader();

  /** @brief Whether the reader references a non-empty filesystem path. */
  [[nodiscard]] bool IsValid() const noexcept;
  /** @brief Return the referenced filesystem path. */
  [[nodiscard]] const std::filesystem::path& GetPath() const noexcept;

  /** @brief Start an asynchronous disk read into a caller-supplied state. */
  [[nodiscard]] bool AsyncReadFromDisk(std::shared_ptr<FileReadingState> state,
                                       Callback callback = {}) const;
  /** @brief Start an asynchronous disk read using an internally created state.
   */
  [[nodiscard]] bool AsyncReadFromDisk(Callback callback = {}) const;

  /** @brief Return the executor used for disk work dispatch. */
  [[nodiscard]] boost::asio::any_io_executor GetWorkExecutor() const noexcept;
  /** @brief Return the executor used for completion callbacks. */
  [[nodiscard]] boost::asio::any_io_executor GetCallbackExecutor()
      const noexcept;

 private:
  struct PrivateTag {};

  FileReader(PrivateTag, std::filesystem::path path,
             boost::asio::any_io_executor work_executor,
             boost::asio::any_io_executor callback_executor);

  std::filesystem::path path_;
  boost::asio::any_io_executor work_executor_;
  boost::asio::any_io_executor callback_executor_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_FILE_FILE_READER_H_
