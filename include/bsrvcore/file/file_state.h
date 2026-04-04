/**
 * @file file_state.h
 * @brief Shared async file operation state objects.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_FILE_FILE_STATE_H_
#define BSRVCORE_FILE_FILE_STATE_H_

#include <cstddef>
#include <filesystem>
#include <memory>
#include <system_error>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore {

class FileReader;
class FileWriter;

/**
 * @brief Shared state populated by FileWriter::AsyncWriteToDisk().
 */
class FileWritingState : public NonCopyableNonMovable<FileWritingState> {
 public:
  /** @brief Create an empty shared write state object. */
  [[nodiscard]] static std::shared_ptr<FileWritingState> Create() {
    struct SharedEnabler final : FileWritingState {
      SharedEnabler() : FileWritingState(PrivateTag{}) {}
    };
    return AllocateShared<SharedEnabler>();
  }

  /** @brief Destination path requested by the write operation. */
  std::filesystem::path path;
  /** @brief Final write error code, default-constructed on success. */
  std::error_code ec;
  /** @brief Number of bytes attempted or written. */
  std::size_t size{0};
  /** @brief Reader created for the written file on success. */
  std::shared_ptr<FileReader> reader;

 private:
  struct PrivateTag {};

  explicit FileWritingState(PrivateTag) {}
};

/**
 * @brief Shared state populated by FileReader::AsyncReadFromDisk().
 */
class FileReadingState : public NonCopyableNonMovable<FileReadingState> {
 public:
  /** @brief Create an empty shared read state object. */
  [[nodiscard]] static std::shared_ptr<FileReadingState> Create() {
    struct SharedEnabler final : FileReadingState {
      SharedEnabler() : FileReadingState(PrivateTag{}) {}
    };
    return AllocateShared<SharedEnabler>();
  }

  /** @brief Source path requested by the read operation. */
  std::filesystem::path path;
  /** @brief Final read error code, default-constructed on success. */
  std::error_code ec;
  /** @brief Number of bytes read from disk. */
  std::size_t size{0};
  /** @brief Writer created from the loaded file content on success. */
  std::shared_ptr<FileWriter> writer;

 private:
  struct PrivateTag {};

  explicit FileReadingState(PrivateTag) {}
};

}  // namespace bsrvcore

#endif  // BSRVCORE_FILE_FILE_STATE_H_
