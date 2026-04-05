/**
 * @file put_processor.cc
 * @brief PutProcessor implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/server/put_processor.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/beast/http/verb.hpp>
#include <filesystem>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/file/file_state.h"

namespace bsrvcore {

std::shared_ptr<PutProcessor> PutProcessor::Create(HttpTaskBase& task) {
  return Create(task.GetRequest(), task.GetExecutor(), task.GetExecutor());
}

std::shared_ptr<PutProcessor> PutProcessor::Create(
    const HttpRequest& request, IoExecutor work_executor,
    IoExecutor callback_executor) {
  struct SharedEnabler final : PutProcessor {
    SharedEnabler(const HttpRequest& request_in, IoExecutor work_executor_in,
                  IoExecutor callback_executor_in)
        : PutProcessor(PrivateTag{}, request_in, std::move(work_executor_in),
                       std::move(callback_executor_in)) {}
  };

  return AllocateShared<SharedEnabler>(request, std::move(work_executor),
                                       std::move(callback_executor));
}

std::shared_ptr<PutProcessor> PutProcessor::Create(const HttpRequest& request,
                                                   IoExecutor executor) {
  return Create(request, executor, executor);
}

PutProcessor::PutProcessor(PrivateTag, const HttpRequest& request,
                           IoExecutor work_executor,
                           IoExecutor callback_executor)
    : work_executor_(std::move(work_executor)),
      callback_executor_(std::move(callback_executor)),
      is_put_(request.method() == HttpVerb::put) {
  if (is_put_) {
    writer_ =
        FileWriter::Create(request.body(), work_executor_, callback_executor_);
  }
}

std::shared_ptr<FileWriter> PutProcessor::GetFileWriter() const noexcept {
  return writer_;
}

bool PutProcessor::AsyncDumpToDisk(std::filesystem::path path,
                                   DumpCallback callback) const {
  if (!writer_ || path.empty()) {
    return false;
  }

  if (!callback) {
    return writer_->AsyncWriteToDisk(std::move(path));
  }

  return writer_->AsyncWriteToDisk(
      std::move(path), FileWritingState::Create(),
      [callback = std::move(callback)](
          const std::shared_ptr<FileWritingState>& state) mutable {
        callback(state && !state->ec);
      });
}

}  // namespace bsrvcore
