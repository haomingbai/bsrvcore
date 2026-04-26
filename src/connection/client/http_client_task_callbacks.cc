
/**
 * @file http_client_task_callbacks.cc
 * @brief Callback plumbing for HttpClientTask::Impl.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <memory>
#include <mutex>
#include <utility>

#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/stream_builder.h"
#include "impl/http_client_task_impl.h"

namespace bsrvcore {

void HttpClientTask::Impl::SetOnConnected(Callback cb) {
  std::scoped_lock const lock(callback_mutex_);
  on_connected_ = std::move(cb);
}

void HttpClientTask::Impl::SetOnHeader(Callback cb) {
  std::scoped_lock const lock(callback_mutex_);
  on_header_ = std::move(cb);
}

void HttpClientTask::Impl::SetOnChunk(Callback cb) {
  std::scoped_lock const lock(callback_mutex_);
  on_chunk_ = std::move(cb);
}

void HttpClientTask::Impl::SetOnDone(Callback cb) {
  std::scoped_lock const lock(callback_mutex_);
  on_done_ = std::move(cb);
}

void HttpClientTask::Impl::SetAssembler(
    std::shared_ptr<RequestAssembler> assembler,
    std::shared_ptr<StreamBuilder> builder) {
  assembler_ = std::move(assembler);
  builder_ = std::move(builder);
}

void HttpClientTask::Impl::SetDoneHook(DoneHook hook) {
  done_hook_ = std::move(hook);
}

}  // namespace bsrvcore
