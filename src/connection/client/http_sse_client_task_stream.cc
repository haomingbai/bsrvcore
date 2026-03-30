/**
 * @file http_sse_client_task_stream.cc
 * @brief Streaming pull logic for HttpSseClientTask::Impl.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "impl/http_sse_client_task_impl.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/post.hpp>
#include <utility>

namespace bsrvcore {

namespace http = http_sse_detail::http;

void HttpSseClientTask::Impl::Next(Callback cb) {
  auto self = shared_from_this();
  boost::asio::post(
      strand_, [self = std::move(self), cb = std::move(cb)]() mutable {
        RunPostedNext(std::move(self), std::move(cb));
      });
}

void HttpSseClientTask::Impl::RunPostedNext(std::shared_ptr<Impl> self,
                                            Callback cb) {
  if (!self->started_ || self->done_) {
    HttpSseClientResult result;
    result.stage = HttpSseClientStage::kNext;
    result.error_stage = HttpSseClientErrorStage::kReadBody;
    result.ec =
        make_error_code(boost::system::errc::operation_not_permitted);
    cb(result);
    return;
  }

  if (self->next_pending_) {
    HttpSseClientResult result;
    result.stage = HttpSseClientStage::kNext;
    result.error_stage = HttpSseClientErrorStage::kReadBody;
    result.ec = make_error_code(boost::system::errc::operation_in_progress);
    cb(result);
    return;
  }

  self->next_pending_ = true;
  self->next_callback_ = std::move(cb);
  self->DoReadNextChunk();
}

void HttpSseClientTask::Impl::DoReadNextChunk() {
  if (use_ssl_) {
    boost::beast::get_lowest_layer(*ssl_stream_)
        .expires_after(options_.read_body_timeout);
    http::async_read_some(
        *ssl_stream_, buffer_, *parser_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 std::size_t) {
              self->OnReadNextChunk(ec);
            }));
    return;
  }

  tcp_stream_->expires_after(options_.read_body_timeout);
  http::async_read_some(
      *tcp_stream_, buffer_, *parser_,
      boost::asio::bind_executor(
          strand_, [self = shared_from_this()](boost::system::error_code ec,
                                               std::size_t) {
            self->OnReadNextChunk(ec);
          }));
}

void HttpSseClientTask::Impl::OnReadNextChunk(boost::system::error_code ec) {
  HttpSseClientResult result;
  result.stage = HttpSseClientStage::kNext;

  if (ec) {
    if (cancelled_ || ec == boost::asio::error::operation_aborted) {
      result.cancelled = true;
      result.ec = ec;
      next_pending_ = false;
      if (next_callback_) {
        next_callback_(result);
      }
      done_ = true;
      return;
    }

    if (ec == http::error::end_of_stream || ec == boost::asio::error::eof) {
      result.eof = true;
      next_pending_ = false;
      if (next_callback_) {
        next_callback_(result);
      }
      done_ = true;
      return;
    }

    failed_ = true;
    error_code_ = ec;
    error_stage_ = HttpSseClientErrorStage::kReadBody;
    result.error_stage = error_stage_;
    result.ec = ec;
    next_pending_ = false;
    if (next_callback_) {
      next_callback_(result);
    }
    done_ = true;
    return;
  }

  const auto& body = parser_->get().body();
  if (body.size() > last_emitted_body_size_) {
    result.chunk = body.substr(last_emitted_body_size_);
    last_emitted_body_size_ = body.size();
  }

  if (parser_->is_done()) {
    result.eof = true;
    done_ = true;
  }

  next_pending_ = false;
  if (next_callback_) {
    next_callback_(result);
  }
}

}  // namespace bsrvcore
