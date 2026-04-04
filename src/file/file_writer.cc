/**
 * @file file_writer.cc
 * @brief FileWriter implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/file/file_writer.h"

#include <boost/asio/post.hpp>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/file/file_reader.h"

namespace bsrvcore {

namespace {

inline void DispatchWriteCallback(const boost::asio::any_io_executor& executor,
                                  std::shared_ptr<FileWritingState> state,
                                  FileWriter::Callback callback) {
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

struct AsyncFileWriteOp {
  AsyncFileWriteOp(std::shared_ptr<FileWriter> writer_in,
                   std::filesystem::path path_in,
                   std::shared_ptr<FileWritingState> state_in,
                   FileWriter::Callback callback_in)
      : writer(std::move(writer_in)),
        path(std::move(path_in)),
        state(std::move(state_in)),
        callback(std::move(callback_in)) {}

  void Run() {
    state->path = path;
    state->size = writer->Size();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      state->ec = std::make_error_code(std::errc::io_error);
      Finish();
      return;
    }

    if (writer->Size() != 0) {
      out.write(writer->Data(), static_cast<std::streamsize>(writer->Size()));
    }
    out.flush();
    if (!out.good()) {
      state->ec = std::make_error_code(std::errc::io_error);
      out.close();
      Finish();
      return;
    }

    out.close();
    state->ec.clear();
    state->reader = FileReader::Create(path, writer->GetWorkExecutor(),
                                       writer->GetCallbackExecutor());
    Finish();
  }

  void Finish() {
    DispatchWriteCallback(writer->GetCallbackExecutor(), std::move(state),
                          std::move(callback));
  }

  std::shared_ptr<FileWriter> writer;
  std::filesystem::path path;
  std::shared_ptr<FileWritingState> state;
  FileWriter::Callback callback;
};

}  // namespace

std::shared_ptr<FileWriter> FileWriter::Create(
    std::string_view payload, boost::asio::any_io_executor work_executor,
    boost::asio::any_io_executor callback_executor) {
  struct SharedEnabler final : FileWriter {
    SharedEnabler(std::string_view payload_in,
                  boost::asio::any_io_executor work_executor_in,
                  boost::asio::any_io_executor callback_executor_in)
        : FileWriter(PrivateTag{}, payload_in, std::move(work_executor_in),
                     std::move(callback_executor_in)) {}
  };

  return AllocateShared<SharedEnabler>(payload, std::move(work_executor),
                                       std::move(callback_executor));
}

std::shared_ptr<FileWriter> FileWriter::Create(
    std::string_view payload, boost::asio::any_io_executor executor) {
  return Create(payload, executor, executor);
}

FileWriter::FileWriter(PrivateTag, std::string_view payload,
                       boost::asio::any_io_executor work_executor,
                       boost::asio::any_io_executor callback_executor)
    : work_executor_(std::move(work_executor)),
      callback_executor_(std::move(callback_executor)) {
  Assign(payload);
}

FileWriter::~FileWriter() {
  if (data_ != nullptr) {
    Deallocate(data_, size_, alignof(char));
  }
}

bool FileWriter::IsValid() const noexcept { return valid_; }

std::size_t FileWriter::Size() const noexcept { return size_; }

const char* FileWriter::Data() const noexcept { return data_; }

bool FileWriter::CopyTo(void* dest, std::size_t dest_size) const noexcept {
  if (!valid_ || ((size_ != 0) && ((dest == nullptr) || (dest_size < size_)))) {
    return false;
  }

  if (size_ != 0) {
    std::memcpy(dest, data_, size_);
  }
  return true;
}

bool FileWriter::AsyncWriteToDisk(std::filesystem::path path,
                                  std::shared_ptr<FileWritingState> state,
                                  Callback callback) const {
  if (!valid_ || !work_executor_ || path.empty() ||
      (callback && !callback_executor_)) {
    return false;
  }

  if (!state) {
    state = FileWritingState::Create();
  }

  auto self = std::const_pointer_cast<FileWriter>(this->shared_from_this());
  auto op = AllocateShared<AsyncFileWriteOp>(
      std::move(self), std::move(path), std::move(state), std::move(callback));
  boost::asio::post(work_executor_,
                    [op = std::move(op)]() mutable { op->Run(); });
  return true;
}

bool FileWriter::AsyncWriteToDisk(std::filesystem::path path,
                                  Callback callback) const {
  return AsyncWriteToDisk(std::move(path), FileWritingState::Create(),
                          std::move(callback));
}

boost::asio::any_io_executor FileWriter::GetWorkExecutor() const noexcept {
  return work_executor_;
}

boost::asio::any_io_executor FileWriter::GetCallbackExecutor() const noexcept {
  return callback_executor_;
}

void FileWriter::Assign(std::string_view payload) {
  size_ = payload.size();
  if (size_ != 0) {
    data_ = static_cast<char*>(Allocate(size_, alignof(char)));
    std::memcpy(data_, payload.data(), size_);
  }
  valid_ = true;
}

}  // namespace bsrvcore
