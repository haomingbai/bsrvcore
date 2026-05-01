/**
 * @file http_sse_client_task_stream.cc
 * @brief Streaming pull logic for HttpSseClientTask::Impl.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/http.hpp>
#include <boost/system/errc.hpp>
#include <cstddef>
#include <memory>
#include <utility>

#include "impl/http_sse_client_task_impl.h"

namespace bsrvcore {

namespace http = http_sse_detail::http;

void HttpSseClientTask::Impl::Next(Callback cb) {
  auto self = shared_from_this();
  // Next() is serialized onto the strand so user code can call it from any
  // thread without racing parser state or overlapping read_some operations.
  boost::asio::post(strand_,
                    [self = std::move(self), cb = std::move(cb)]() mutable {
                      RunPostedNext(std::move(self), std::move(cb));
                    });
}

void HttpSseClientTask::Impl::RunPostedNext(const std::shared_ptr<Impl>& self,
                                            Callback cb) {
  if (self->lifecycle_state_ != LifecycleState::kStreaming) {
    HttpSseClientResult result;
    result.stage = HttpSseClientStage::kNext;
    result.error_stage = HttpSseClientErrorStage::kReadBody;
    result.ec = make_error_code(boost::system::errc::operation_not_permitted);
    self->DispatchCallback(std::move(cb), std::move(result));
    return;
  }

  if (self->next_read_state_ == NextReadState::kPending) {
    HttpSseClientResult result;
    result.stage = HttpSseClientStage::kNext;
    result.error_stage = HttpSseClientErrorStage::kReadBody;
    result.ec = make_error_code(boost::system::errc::operation_in_progress);
    self->DispatchCallback(std::move(cb), std::move(result));
    return;
  }

  self->next_read_state_ = NextReadState::kPending;
  self->next_callback_ = std::move(cb);
  // Exactly one outstanding Next() maps to exactly one async_read_some() call.
  // This keeps the incremental parser contract simple for consumers.
  self->DoReadNextChunk();
}

void HttpSseClientTask::Impl::DoReadNextChunk() {
  if (stream_.IsSsl()) {
    auto& stream = stream_.Ssl();
    boost::beast::get_lowest_layer(stream).expires_after(
        options_.read_body_timeout);
    http::async_read_some(
        stream, buffer_, *parser_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 std::size_t) {
              self->OnReadNextChunk(ec);
            }));
    return;
  }

  auto& stream = stream_.Tcp();
  stream.expires_after(options_.read_body_timeout);
  http::async_read_some(
      stream, buffer_, *parser_,
      boost::asio::bind_executor(
          strand_, [self = shared_from_this()](boost::system::error_code ec,
                                               std::size_t) {
            self->OnReadNextChunk(ec);
          }));
}

void HttpSseClientTask::Impl::OnReadNextChunk(boost::system::error_code ec) {
  // Call chain: DoReadNextChunk → async_read_some → OnReadNextChunk
  //   → TerminateSseRead (on error/EOF/done)
  //   → dispatch chunk delta (on success with more data)
  HttpSseClientResult result;
  result.stage = HttpSseClientStage::kNext;

  if (ec) {
    // Error path: classify the error and terminate.
    if (cancellation_state_ == CancellationState::kRequested ||
        ec == boost::asio::error::operation_aborted) {
      result.cancelled = true;
      result.ec = ec;
      termination_state_ = TerminationState::kCancelled;
    } else if (ec == http::error::end_of_stream ||
               ec == boost::asio::error::eof) {
      result.eof = true;
      termination_state_ = TerminationState::kEof;
    } else {
      error_code_ = ec;
      error_stage_ = HttpSseClientErrorStage::kReadBody;
      result.error_stage = error_stage_;
      result.ec = ec;
      termination_state_ = TerminationState::kFailure;
    }
    lifecycle_state_ = LifecycleState::kDone;
    TerminateSseRead(std::move(result));
    return;
  }

  // Success path: extract the newly appended body suffix.
  const auto& body = parser_->get().body();
  if (body.size() > last_emitted_body_size_) {
    // Beast's parser body is cumulative. Expose only the newly appended suffix
    // so callers can feed an incremental SSE parser without reprocessing the
    // full response buffer on every callback.
    result.chunk = body.substr(last_emitted_body_size_);
    last_emitted_body_size_ = body.size();
  }

  if (parser_->is_done()) {
    result.eof = true;
    termination_state_ = TerminationState::kEof;
    lifecycle_state_ = LifecycleState::kDone;
    TerminateSseRead(std::move(result));
    return;
  }

  // More data expected — dispatch chunk and reset read state.
  next_read_state_ = NextReadState::kIdle;
  Callback const callback = std::move(next_callback_);
  DispatchCallback(callback, std::move(result));
}

void HttpSseClientTask::Impl::TerminateSseRead(HttpSseClientResult result) {
  // Finalize: reset read state and dispatch the terminal result.
  next_read_state_ = NextReadState::kIdle;
  Callback const callback = std::move(next_callback_);
  DispatchCallback(callback, std::move(result));
}

}  // namespace bsrvcore
