/**
 * @file http_sse_client_task_startup.cc
 * @brief Connection setup flow for HttpSseClientTask::Impl.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/http.hpp>
#include <boost/system/errc.hpp>
#include <cctype>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/stream_builder.h"
#include "impl/http_sse_client_task_impl.h"

namespace bsrvcore {

namespace http = http_sse_detail::http;

namespace {

std::string ToLower(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

}  // namespace

HttpSseClientTask::Impl::Impl(HttpSseClientTask::Executor io_executor,
                              HttpSseClientTask::Executor callback_executor,
                              std::string host, std::string port,
                              std::string target, HttpSseClientOptions options,
                              bool use_ssl, SslContextPtr ssl_ctx)
    : io_executor_(std::move(io_executor)),
      callback_executor_(std::move(callback_executor)),
      strand_(io_executor_),
      host_(std::move(host)),
      port_(std::move(port)),
      target_(std::move(target)),
      options_(std::move(options)),
      use_ssl_(use_ssl),
      ssl_ctx_(std::move(ssl_ctx)) {
  request_.method(http::verb::get);
  request_.version(11);
  request_.target(target_);
  request_.set(http::field::host, host_);
  request_.set(http::field::accept, "text/event-stream");
  request_.set(http::field::cache_control, "no-cache");
  if (!options_.user_agent.empty()) {
    request_.set(http::field::user_agent, options_.user_agent);
  }
  request_.keep_alive(options_.keep_alive);
}

HttpRequest& HttpSseClientTask::Impl::Request() noexcept { return request_; }

void HttpSseClientTask::Impl::Start(Callback cb) {
  auto self = shared_from_this();
  boost::asio::post(strand_,
                    [self = std::move(self), cb = std::move(cb)]() mutable {
                      RunPostedStart(std::move(self), std::move(cb));
                    });
}

void HttpSseClientTask::Impl::Cancel() {
  auto self = shared_from_this();
  boost::asio::post(strand_,
                    [self = std::move(self)]() { RunPostedCancel(self); });
}

bool HttpSseClientTask::Impl::Failed() const noexcept {
  return termination_state_ == TerminationState::kFailure;
}

boost::system::error_code HttpSseClientTask::Impl::ErrorCode() const noexcept {
  return error_code_;
}

HttpSseClientErrorStage HttpSseClientTask::Impl::ErrorStage() const noexcept {
  return error_stage_;
}

void HttpSseClientTask::Impl::SetCreateError(
    boost::system::error_code ec, HttpSseClientErrorStage error_stage) {
  create_error_ = ec;
  create_error_stage_ = error_stage;
}

void HttpSseClientTask::Impl::SetRawTcpStream(TcpStream stream) {
  // Raw mode: directly assign to working stream, no assembler/builder needed.
  stream_.EmplaceTcp(std::move(stream));
}

void HttpSseClientTask::Impl::SetRawSslStream(SslStream stream) {
  // Raw mode: directly assign to working stream, no assembler/builder needed.
  stream_.EmplaceSsl(std::move(stream));
}

void HttpSseClientTask::Impl::SetAssembler(
    std::shared_ptr<RequestAssembler> assembler,
    std::shared_ptr<StreamBuilder> builder) {
  assembler_ = std::move(assembler);
  builder_ = std::move(builder);
}

void HttpSseClientTask::Impl::DispatchCallback(
    Callback cb, HttpSseClientResult result) const {
  if (!cb) {
    return;
  }

  boost::asio::post(callback_executor_,
                    [cb = std::move(cb), result = std::move(result)]() mutable {
                      cb(result);
                    });
}

void HttpSseClientTask::Impl::RunPostedStart(const std::shared_ptr<Impl>& self,
                                             Callback cb) {
  self->start_callback_ = std::move(cb);
  self->DoStart();
}

void HttpSseClientTask::Impl::RunPostedCancel(
    const std::shared_ptr<Impl>& self) {
  self->DoCancel();
}

void HttpSseClientTask::Impl::DoStart() {
  if (lifecycle_state_ != LifecycleState::kIdle) {
    return;
  }
  lifecycle_state_ = LifecycleState::kStarting;

  if (create_error_) {
    FailStart(create_error_stage_, *create_error_);
    return;
  }

  // Raw mode: streams already assigned via SetRawTcpStream/SetRawSslStream.
  if (!assembler_) {
    if ((use_ssl_ && !stream_.IsSsl()) || (!use_ssl_ && !stream_.IsTcp())) {
      FailStart(HttpSseClientErrorStage::kCreate,
                make_error_code(boost::system::errc::invalid_argument));
      return;
    }
    DoWriteRequest();
    return;
  }

  DoAcquire();
}

void HttpSseClientTask::Impl::DoAcquire() {
  // Call chain: DoStart → DoAcquire → assembler_.Assemble
  //   → builder_.Acquire → OnAcquireComplete
  //
  // Assembler prepares the request (headers, cookies, payload).
  // Builder resolves DNS, connects TCP, and optionally handshakes TLS.
  auto result = assembler_->Assemble(
      request_, options_, use_ssl_ ? "https" : "http", host_, port_, ssl_ctx_);
  request_ = std::move(result.request);
  connection_key_ = result.connection_key;

  auto self = shared_from_this();
  builder_->Acquire(
      connection_key_, io_executor_,
      boost::asio::bind_executor(
          strand_, [self](boost::system::error_code ec, StreamSlot slot) {
            self->OnAcquireComplete(ec, std::move(slot));
          }));
}

void HttpSseClientTask::Impl::OnAcquireComplete(boost::system::error_code ec,
                                                StreamSlot slot) {
  if (ec) {
    const bool is_ssl_error =
        ec.category() == boost::asio::error::get_ssl_category();
    HttpSseClientErrorStage stage = is_ssl_error
                                        ? HttpSseClientErrorStage::kTlsHandshake
                                        : HttpSseClientErrorStage::kConnect;
    FailStart(stage, ec);
    return;
  }

  if (!slot.Stream().HasStream()) {
    FailStart(HttpSseClientErrorStage::kConnect,
              make_error_code(boost::system::errc::not_connected));
    return;
  }

  stream_ = std::move(slot.Stream());
  DoWriteRequest();
}

void HttpSseClientTask::Impl::DoWriteRequest() {
  request_.prepare_payload();

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

void HttpSseClientTask::Impl::OnWriteRequest(boost::system::error_code ec) {
  if (ec) {
    FailStart(HttpSseClientErrorStage::kWriteRequest, ec);
    return;
  }

  parser_.emplace();
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

void HttpSseClientTask::Impl::OnReadHeader(boost::system::error_code ec) {
  if (ec) {
    FailStart(HttpSseClientErrorStage::kReadHeader, ec);
    return;
  }

  const auto& msg = parser_->get();
  if (msg.result() != http::status::ok) {
    FailStart(HttpSseClientErrorStage::kReadHeader,
              make_error_code(boost::system::errc::protocol_error));
    return;
  }

  const auto it = msg.base().find(http::field::content_type);
  if (it == msg.base().end() ||
      ToLower(std::string(it->value())).find("text/event-stream") ==
          std::string::npos) {
    FailStart(HttpSseClientErrorStage::kReadHeader,
              make_error_code(boost::system::errc::protocol_error));
    return;
  }

  // Sync cookies from response header via assembler (if assembled mode).
  if (assembler_) {
    assembler_->OnResponseHeader(msg.base(), host_, target_);
  }

  HttpSseClientResult result;
  result.stage = HttpSseClientStage::kStart;
  result.error_stage = HttpSseClientErrorStage::kNone;
  result.header = msg.base();
  lifecycle_state_ = LifecycleState::kStreaming;
  Callback const callback = std::move(start_callback_);
  DispatchCallback(callback, std::move(result));
}

void HttpSseClientTask::Impl::FailStart(HttpSseClientErrorStage error_stage,
                                        boost::system::error_code ec) {
  if (lifecycle_state_ == LifecycleState::kDone) {
    return;
  }

  error_stage_ = error_stage;
  error_code_ = ec;

  HttpSseClientResult result;
  result.stage = HttpSseClientStage::kStart;
  result.error_stage = error_stage;
  result.ec = ec;
  result.cancelled = cancellation_state_ == CancellationState::kRequested ||
                     (ec == boost::asio::error::operation_aborted);
  termination_state_ = result.cancelled ? TerminationState::kCancelled
                                        : TerminationState::kFailure;

  lifecycle_state_ = LifecycleState::kDone;

  Callback const callback = std::move(start_callback_);
  DispatchCallback(callback, std::move(result));
}

void HttpSseClientTask::Impl::DoCancel() {
  if (lifecycle_state_ == LifecycleState::kDone) {
    return;
  }

  cancellation_state_ = CancellationState::kRequested;

  stream_.Close();
}

}  // namespace bsrvcore
