
/**
 * @file http_client_task_flow.cc
 * @brief Async transport flow for HttpClientTask::Impl.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/basic_stream.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/optional/optional.hpp>
#include <boost/system/errc.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "bsrvcore/connection/client/client_stream.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/stream_builder.h"
#include "bsrvcore/connection/client/stream_slot.h"
#include "bsrvcore/core/types.h"
#include "impl/http_client_task_impl.h"

namespace boost {
namespace beast {
namespace http {
enum class verb;
}  // namespace http
}  // namespace beast
}  // namespace boost

namespace bsrvcore {

namespace http = http_client_detail::http;

// ============================================================================
// Construction
// ============================================================================

HttpClientTask::Impl::Impl(HttpClientTask::Executor io_executor,
                           HttpClientTask::Executor callback_executor,
                           std::string host, std::string port,
                           std::string target,
                           http_client_detail::http::verb method,
                           HttpClientOptions options, bool use_ssl,
                           SslContextPtr ssl_ctx)
    : io_executor_(std::move(io_executor)),
      callback_executor_(std::move(callback_executor)),
      strand_(io_executor_),
      host_(std::move(host)),
      port_(std::move(port)),
      target_(std::move(target)),
      options_(std::move(options)),
      use_ssl_(use_ssl),
      ssl_ctx_(std::move(ssl_ctx)) {
  request_.method(method);
  request_.target(target_);
  request_.version(11);
}

HttpClientRequest& HttpClientTask::Impl::Request() noexcept { return request_; }

void HttpClientTask::Impl::Start() {
  auto self = shared_from_this();
  boost::asio::post(strand_,
                    [self = std::move(self)]() { RunPostedStart(self); });
}

void HttpClientTask::Impl::Cancel() {
  auto self = shared_from_this();
  boost::asio::post(strand_,
                    [self = std::move(self)]() { RunPostedCancel(self); });
}

bool HttpClientTask::Impl::Failed() const noexcept {
  return completion_state_ == CompletionState::kFailure ||
         completion_state_ == CompletionState::kCancelled;
}

boost::system::error_code HttpClientTask::Impl::ErrorCode() const noexcept {
  return error_code_;
}

HttpClientErrorStage HttpClientTask::Impl::ErrorStage() const noexcept {
  return error_stage_;
}

void HttpClientTask::Impl::SetCreateError(boost::system::error_code ec,
                                          HttpClientErrorStage error_stage) {
  create_error_ = ec;
  create_error_stage_ = error_stage;
}

void HttpClientTask::Impl::SetRawTcpStream(TcpStream stream) {
  stream_.EmplaceTcp(std::move(stream));
}

void HttpClientTask::Impl::SetRawSslStream(SslStream stream) {
  stream_.EmplaceSsl(std::move(stream));
}

// ============================================================================
// Startup: DoStart → DoAcquire → OnAcquireComplete
// ============================================================================

void HttpClientTask::Impl::RunPostedStart(const std::shared_ptr<Impl>& self) {
  self->DoStart();
}

void HttpClientTask::Impl::RunPostedCancel(const std::shared_ptr<Impl>& self) {
  self->DoCancel();
}

void HttpClientTask::Impl::DoStart() {
  if (lifecycle_state_ != LifecycleState::kIdle) {
    return;
  }
  lifecycle_state_ = LifecycleState::kRunning;

  if (create_error_) {
    Fail(create_error_stage_, *create_error_);
    return;
  }

  if (assembler_) {
    DoAcquire();
    return;
  }

  // Raw mode: stream already provided, validate it before proceeding.
  if ((use_ssl_ && !stream_.IsSsl()) || (!use_ssl_ && !stream_.IsTcp())) {
    Fail(HttpClientErrorStage::kCreate,
         make_error_code(boost::system::errc::invalid_argument));
    return;
  }

  EmitConnected(boost::system::error_code{});
  DoWriteRequest();
}

void HttpClientTask::Impl::DoAcquire() {
  const std::string scheme = use_ssl_ ? "https" : "http";
  auto result =
      assembler_->Assemble(request_, options_, scheme, host_, port_, ssl_ctx_);
  request_ = std::move(result.request);
  connection_key_ = result.connection_key;
  use_ssl_ = (connection_key_.scheme == "https");
  host_ = connection_key_.host;
  port_ = connection_key_.port;
  ssl_ctx_ = connection_key_.ssl_ctx;
  target_ = std::string(request_.target());

  builder_->Acquire(
      connection_key_, io_executor_,
      boost::asio::bind_executor(
          strand_, [self = shared_from_this()](boost::system::error_code ec,
                                               StreamSlot slot) {
            self->OnAcquireComplete(ec, std::move(slot));
          }));
}

void HttpClientTask::Impl::OnAcquireComplete(boost::system::error_code ec,
                                             StreamSlot slot) {
  if (ec) {
    Fail(HttpClientErrorStage::kConnect, ec);
    return;
  }

  if (!slot.Stream().HasStream()) {
    Fail(HttpClientErrorStage::kConnect,
         make_error_code(boost::system::errc::invalid_argument));
    return;
  }

  stream_ = std::move(slot.Stream());
  EmitConnected(boost::system::error_code{});
  DoWriteRequest();
}

// ============================================================================
// Write request
// ============================================================================

void HttpClientTask::Impl::DoWriteRequest() {
  if (stream_.IsSsl()) {
    auto& stream = stream_.Ssl();
    boost::beast::get_lowest_layer(stream).expires_after(
        options_.write_timeout);
    http::async_write(
        stream, request_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 std::size_t) {
              self->OnWriteRequest(ec);
            }));
    return;
  }

  auto& stream = stream_.Tcp();
  stream.expires_after(options_.write_timeout);
  http::async_write(
      stream, request_,
      boost::asio::bind_executor(
          strand_, [self = shared_from_this()](boost::system::error_code ec,
                                               std::size_t) {
            self->OnWriteRequest(ec);
          }));
}

void HttpClientTask::Impl::OnWriteRequest(boost::system::error_code ec) {
  if (ec) {
    Fail(HttpClientErrorStage::kWriteRequest, ec);
    return;
  }

  parser_.emplace();
  parser_->body_limit(options_.max_response_body_bytes);

  if (stream_.IsSsl()) {
    auto& stream = stream_.Ssl();
    boost::beast::get_lowest_layer(stream).expires_after(
        options_.read_header_timeout);
    http::async_read_header(
        stream, buffer_, *parser_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 std::size_t) {
              self->OnReadHeader(ec);
            }));
    return;
  }

  auto& stream = stream_.Tcp();
  stream.expires_after(options_.read_header_timeout);
  http::async_read_header(
      stream, buffer_, *parser_,
      boost::asio::bind_executor(
          strand_, [self = shared_from_this()](boost::system::error_code ec,
                                               std::size_t) {
            self->OnReadHeader(ec);
          }));
}

// ============================================================================
// Read response
// ============================================================================

void HttpClientTask::Impl::OnReadHeader(boost::system::error_code ec) {
  if (ec) {
    Fail(HttpClientErrorStage::kReadHeader, ec);
    return;
  }

  HttpResponseHeader header;
  const auto& msg = parser_->get();
  header.version(msg.version());
  header.result(msg.result());
  for (const auto& f : msg.base()) {
    header.set(f.name_string(), f.value());
  }

  // Notify assembler for cookie sync or other header processing.
  if (assembler_) {
    assembler_->OnResponseHeader(header, host_, target_);
  }

  EmitHeader(header, boost::system::error_code{});

  if (HasChunkCallback()) {
    last_emitted_body_size_ = parser_->get().body().size();
    DoReadBodySome();
    return;
  }

  DoReadBodyAll();
}

void HttpClientTask::Impl::DoReadBodyAll() {
  if (stream_.IsSsl()) {
    auto& stream = stream_.Ssl();
    boost::beast::get_lowest_layer(stream).expires_after(
        options_.read_body_timeout);
    http::async_read(
        stream, buffer_, *parser_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 std::size_t) {
              self->OnReadBodyAll(ec);
            }));
    return;
  }

  auto& stream = stream_.Tcp();
  stream.expires_after(options_.read_body_timeout);
  http::async_read(stream, buffer_, *parser_,
                   boost::asio::bind_executor(
                       strand_, [self = shared_from_this()](
                                    boost::system::error_code ec, std::size_t) {
                         self->OnReadBodyAll(ec);
                       }));
}

void HttpClientTask::Impl::OnReadBodyAll(boost::system::error_code ec) {
  if (ec && ec != http::error::end_of_stream &&
      ec != http::error::partial_message) {
    Fail(HttpClientErrorStage::kReadBody, ec);
    return;
  }

  Succeed(parser_->release());
}

void HttpClientTask::Impl::DoReadBodySome() {
  if (stream_.IsSsl()) {
    auto& stream = stream_.Ssl();
    boost::beast::get_lowest_layer(stream).expires_after(
        options_.read_body_timeout);
    http::async_read_some(
        stream, buffer_, *parser_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 std::size_t) {
              self->OnReadBodySome(ec);
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
            self->OnReadBodySome(ec);
          }));
}

void HttpClientTask::Impl::OnReadBodySome(boost::system::error_code ec) {
  if (ec && ec != http::error::end_of_stream &&
      ec != http::error::partial_message) {
    Fail(HttpClientErrorStage::kReadBody, ec);
    return;
  }

  const auto& body = parser_->get().body();
  if (body.size() > last_emitted_body_size_) {
    EmitChunk(std::string(body.data() + last_emitted_body_size_,
                          body.size() - last_emitted_body_size_));
    last_emitted_body_size_ = body.size();
  }

  if (parser_->is_done()) {
    Succeed(parser_->release());
    return;
  }

  DoReadBodySome();
}

// ============================================================================
// Cancel / Close
// ============================================================================

void HttpClientTask::Impl::DoCancel() {
  if (lifecycle_state_ == LifecycleState::kDone) {
    return;
  }

  cancellation_state_ = CancellationState::kRequested;
  CloseTransports();
}

void HttpClientTask::Impl::CloseTransports() { stream_.Close(); }

// ============================================================================
// Completion
// ============================================================================

void HttpClientTask::Impl::Fail(HttpClientErrorStage error_stage,
                                boost::system::error_code ec) {
  if (lifecycle_state_ == LifecycleState::kDone) {
    return;
  }

  error_stage_ = error_stage;
  error_code_ = ec;

  HttpClientResult stage_result;
  stage_result.ec = ec;
  stage_result.cancelled =
      cancellation_state_ == CancellationState::kRequested ||
      (ec == boost::asio::error::operation_aborted);
  completion_state_ = stage_result.cancelled ? CompletionState::kCancelled
                                             : CompletionState::kFailure;
  stage_result.error_stage = error_stage;
  stage_result.stage = ErrorStageToCallbackStage(error_stage);
  EmitStageByResult(stage_result);

  auto done_result = stage_result;
  done_result.stage = HttpClientStage::kDone;
  EmitDone(done_result);
  lifecycle_state_ = LifecycleState::kDone;

  // Return stream to builder on failure (slot may still be reusable).
  if (done_hook_) {
    StreamSlot slot;
    slot.key = connection_key_;
    slot.SetStream(std::move(stream_));
    slot.http_version = 11;
    slot.upstream_closed = true;
    done_hook_(std::move(slot));
  }
}

void HttpClientTask::Impl::Succeed(HttpClientResponse response) {
  if (lifecycle_state_ == LifecycleState::kDone) {
    return;
  }

  completion_state_ = CompletionState::kSuccess;
  const bool keep_alive = response.keep_alive();

  HttpClientResult result;
  result.ec = {};
  result.cancelled = false;
  result.stage = HttpClientStage::kDone;
  result.response = std::move(response);
  EmitDone(result);
  lifecycle_state_ = LifecycleState::kDone;

  // Return stream to builder.
  if (done_hook_) {
    StreamSlot slot;
    slot.key = connection_key_;
    slot.SetStream(std::move(stream_));
    slot.http_version = 11;
    slot.upstream_closed = !keep_alive;
    done_hook_(std::move(slot));
  }
}

// ============================================================================
// Callback emission
// ============================================================================

void HttpClientTask::Impl::EmitConnected(boost::system::error_code ec) {
  auto cb = GetCallbackCopy(HttpClientStage::kConnected);
  if (!cb) return;
  HttpClientResult result;
  result.ec = ec;
  result.stage = HttpClientStage::kConnected;
  DispatchCallback(std::move(cb), std::move(result));
}

void HttpClientTask::Impl::EmitHeader(const HttpResponseHeader& header,
                                      boost::system::error_code ec) {
  auto cb = GetCallbackCopy(HttpClientStage::kHeader);
  if (!cb) return;
  HttpClientResult result;
  result.ec = ec;
  result.stage = HttpClientStage::kHeader;
  result.header = header;
  DispatchCallback(std::move(cb), std::move(result));
}

void HttpClientTask::Impl::EmitChunk(std::string chunk) {
  auto cb = GetCallbackCopy(HttpClientStage::kChunk);
  if (!cb) return;
  HttpClientResult result;
  result.stage = HttpClientStage::kChunk;
  result.chunk = std::move(chunk);
  DispatchCallback(std::move(cb), std::move(result));
}

void HttpClientTask::Impl::EmitDone(const HttpClientResult& result) {
  auto cb = GetDoneCallbackCopy();
  if (!cb) return;
  DispatchCallback(std::move(cb), result);
}

void HttpClientTask::Impl::EmitStageByResult(const HttpClientResult& result) {
  auto cb = GetCallbackCopy(result.stage);
  if (!cb) return;
  DispatchCallback(std::move(cb), result);
}

bool HttpClientTask::Impl::HasChunkCallback() const {
  std::scoped_lock const lock(callback_mutex_);
  return static_cast<bool>(on_chunk_);
}

HttpClientTask::Impl::Callback HttpClientTask::Impl::GetCallbackCopy(
    HttpClientStage stage) const {
  std::scoped_lock const lock(callback_mutex_);
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
  return nullptr;
}

HttpClientTask::Impl::Callback HttpClientTask::Impl::GetDoneCallbackCopy()
    const {
  std::scoped_lock const lock(callback_mutex_);
  return on_done_;
}

void HttpClientTask::Impl::DispatchCallback(Callback cb,
                                            HttpClientResult result) const {
  boost::asio::post(callback_executor_,
                    [cb = std::move(cb), result = std::move(result)]() mutable {
                      cb(std::move(result));
                    });
}

HttpClientStage HttpClientTask::Impl::ErrorStageToCallbackStage(
    HttpClientErrorStage error_stage) {
  switch (error_stage) {
    case HttpClientErrorStage::kCreate:
    case HttpClientErrorStage::kResolve:
    case HttpClientErrorStage::kConnect:
    case HttpClientErrorStage::kTlsHandshake:
      return HttpClientStage::kConnected;
    case HttpClientErrorStage::kWriteRequest:
    case HttpClientErrorStage::kReadHeader:
      return HttpClientStage::kHeader;
    case HttpClientErrorStage::kReadBody:
      return HttpClientStage::kChunk;
    case HttpClientErrorStage::kNone:
      return HttpClientStage::kDone;
  }
  return HttpClientStage::kDone;
}

}  // namespace bsrvcore
