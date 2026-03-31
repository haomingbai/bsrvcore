/**
 * @file http_client_task_callbacks.cc
 * @brief Callback plumbing for HttpClientTask::Impl.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "impl/http_client_task_impl.h"

namespace bsrvcore {

void HttpClientTask::Impl::SetSession(
    std::weak_ptr<HttpClientSession> session) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  session_ = std::move(session);
}

void HttpClientTask::Impl::SetOnConnected(Callback cb) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  on_connected_ = std::move(cb);
}

void HttpClientTask::Impl::SetOnHeader(Callback cb) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  on_header_ = std::move(cb);
}

void HttpClientTask::Impl::SetOnChunk(Callback cb) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  on_chunk_ = std::move(cb);
}

void HttpClientTask::Impl::SetOnDone(Callback cb) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  on_done_ = std::move(cb);
}

void HttpClientTask::Impl::EmitConnected(boost::system::error_code ec) {
  HttpClientResult result;
  result.ec = ec;
  result.stage = HttpClientStage::kConnected;
  result.cancelled = cancelled_;
  result.error_stage = HttpClientErrorStage::kNone;

  auto cb = GetCallbackCopy(HttpClientStage::kConnected);
  if (cb) {
    cb(result);
  }
}

void HttpClientTask::Impl::EmitHeader(const HttpResponseHeader& header,
                                      boost::system::error_code ec) {
  HttpClientResult result;
  result.ec = ec;
  result.stage = HttpClientStage::kHeader;
  result.cancelled = cancelled_;
  result.error_stage = HttpClientErrorStage::kNone;
  result.header = header;

  auto cb = GetCallbackCopy(HttpClientStage::kHeader);
  if (cb) {
    cb(result);
  }
}

void HttpClientTask::Impl::EmitChunk(std::string chunk) {
  HttpClientResult result;
  result.stage = HttpClientStage::kChunk;
  result.cancelled = cancelled_;
  result.error_stage = HttpClientErrorStage::kNone;
  result.chunk = std::move(chunk);

  auto cb = GetCallbackCopy(HttpClientStage::kChunk);
  if (cb) {
    cb(result);
  }
}

void HttpClientTask::Impl::EmitDone(const HttpClientResult& result) {
  auto cb = GetDoneCallbackCopy();
  if (cb) {
    cb(result);
  }
}

void HttpClientTask::Impl::EmitStageByResult(const HttpClientResult& result) {
  auto cb = GetCallbackCopy(result.stage);
  if (cb) {
    cb(result);
  }
}

bool HttpClientTask::Impl::HasChunkCallback() const {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  return static_cast<bool>(on_chunk_);
}

HttpClientTask::Impl::Callback HttpClientTask::Impl::GetCallbackCopy(
    HttpClientStage stage) const {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  switch (stage) {
    case HttpClientStage::kConnected:
      return on_connected_;
    case HttpClientStage::kHeader:
      return on_header_;
    case HttpClientStage::kChunk:
      return on_chunk_;
    case HttpClientStage::kDone:
      return on_done_;
  }
  return {};
}

HttpClientTask::Impl::Callback HttpClientTask::Impl::GetDoneCallbackCopy()
    const {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  return on_done_;
}

HttpClientStage HttpClientTask::Impl::ErrorStageToCallbackStage(
    HttpClientErrorStage error_stage) {
  switch (error_stage) {
    case HttpClientErrorStage::kReadHeader:
      return HttpClientStage::kHeader;
    case HttpClientErrorStage::kReadBody:
      return HttpClientStage::kChunk;
    case HttpClientErrorStage::kNone:
    case HttpClientErrorStage::kCreate:
    case HttpClientErrorStage::kResolve:
    case HttpClientErrorStage::kConnect:
    case HttpClientErrorStage::kTlsHandshake:
    case HttpClientErrorStage::kWriteRequest:
    default:
      return HttpClientStage::kConnected;
  }
}

}  // namespace bsrvcore
