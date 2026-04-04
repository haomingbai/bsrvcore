/**
 * @file file_reader.cc
 * @brief FileReader implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/file/file_reader.h"

#include <boost/asio/post.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/file/file_writer.h"

namespace bsrvcore {

namespace {

inline void DispatchReadCallback(const boost::asio::any_io_executor& executor,
                                 std::shared_ptr<FileReadingState> state,
                                 FileReader::Callback callback) {
  if (!callback) {
    return;
  }

  if (executor) {
    boost::asio::post(executor, [state = std::move(state),
                                 callback = std::move(callback)]() mutable {
      callback(std::move(state));
    });
    return;
  }

  callback(std::move(state));
}

struct AsyncFileReadOp {
  AsyncFileReadOp(std::shared_ptr<FileReader> reader_in,
                  std::shared_ptr<FileReadingState> state_in,
                  FileReader::Callback callback_in)
      : reader(std::move(reader_in)),
        state(std::move(state_in)),
        callback(std::move(callback_in)) {}

  void Run() {
    state->path = reader->GetPath();

    std::error_code fs_ec;
    const auto size = std::filesystem::file_size(reader->GetPath(), fs_ec);
    if (fs_ec) {
      state->ec = fs_ec;
      Finish();
      return;
    }

    state->size = static_cast<std::size_t>(size);
    std::string buffer(state->size, '\0');
    std::ifstream in(reader->GetPath(), std::ios::binary);
    if (!in.is_open()) {
      state->ec = std::make_error_code(std::errc::io_error);
      Finish();
      return;
    }

    if (state->size != 0) {
      in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      if (!in.good() && !in.eof()) {
        state->ec = std::make_error_code(std::errc::io_error);
        Finish();
        return;
      }
    }

    state->ec.clear();
    state->writer = FileWriter::Create(
        std::string_view(buffer.data(), buffer.size()),
        reader->GetWorkExecutor(), reader->GetCallbackExecutor());
    Finish();
  }

  void Finish() {
    DispatchReadCallback(reader->GetCallbackExecutor(), std::move(state),
                         std::move(callback));
  }

  std::shared_ptr<FileReader> reader;
  std::shared_ptr<FileReadingState> state;
  FileReader::Callback callback;
};

}  // namespace

std::shared_ptr<FileReader> FileReader::Create(
    std::filesystem::path path, boost::asio::any_io_executor work_executor,
    boost::asio::any_io_executor callback_executor) {
  struct SharedEnabler final : FileReader {
    SharedEnabler(std::filesystem::path path_in,
                  boost::asio::any_io_executor work_executor_in,
                  boost::asio::any_io_executor callback_executor_in)
        : FileReader(PrivateTag{}, std::move(path_in),
                     std::move(work_executor_in),
                     std::move(callback_executor_in)) {}
  };

  return AllocateShared<SharedEnabler>(
      std::move(path), std::move(work_executor), std::move(callback_executor));
}

std::shared_ptr<FileReader> FileReader::Create(
    std::filesystem::path path, boost::asio::any_io_executor executor) {
  return Create(std::move(path), executor, executor);
}

FileReader::FileReader(PrivateTag, std::filesystem::path path,
                       boost::asio::any_io_executor work_executor,
                       boost::asio::any_io_executor callback_executor)
    : path_(std::move(path)),
      work_executor_(std::move(work_executor)),
      callback_executor_(std::move(callback_executor)) {}

FileReader::~FileReader() = default;

bool FileReader::IsValid() const noexcept { return !path_.empty(); }

const std::filesystem::path& FileReader::GetPath() const noexcept {
  return path_;
}

bool FileReader::AsyncReadFromDisk(std::shared_ptr<FileReadingState> state,
                                   Callback callback) const {
  if (!IsValid() || !work_executor_ || (callback && !callback_executor_)) {
    return false;
  }

  if (!state) {
    state = FileReadingState::Create();
  }

  auto self = std::const_pointer_cast<FileReader>(this->shared_from_this());
  auto op = AllocateShared<AsyncFileReadOp>(std::move(self), std::move(state),
                                            std::move(callback));
  boost::asio::post(work_executor_,
                    [op = std::move(op)]() mutable { op->Run(); });
  return true;
}

bool FileReader::AsyncReadFromDisk(Callback callback) const {
  return AsyncReadFromDisk(FileReadingState::Create(), std::move(callback));
}

boost::asio::any_io_executor FileReader::GetWorkExecutor() const noexcept {
  return work_executor_;
}

boost::asio::any_io_executor FileReader::GetCallbackExecutor() const noexcept {
  return callback_executor_;
}

}  // namespace bsrvcore
