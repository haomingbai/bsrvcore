/**
 * @file http_sse_client_task_impl.h
 * @brief Internal HttpSseClientTask implementation declaration.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_IMPL_HTTP_SSE_CLIENT_TASK_IMPL_H_
#define BSRVCORE_CONNECTION_CLIENT_IMPL_HTTP_SSE_CLIENT_TASK_IMPL_H_

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "bsrvcore/connection/client/http_sse_client_task.h"
#include "bsrvcore/connection/client/stream_slot.h"

// Forward declarations for assembled mode.
namespace bsrvcore {
class RequestAssembler;
class StreamBuilder;
}  // namespace bsrvcore

namespace bsrvcore {

namespace http_sse_detail {
namespace http = boost::beast::http;
}  // namespace http_sse_detail

class HttpSseClientTask::Impl
    : public std::enable_shared_from_this<HttpSseClientTask::Impl> {
 public:
  using Callback = HttpSseClientTask::Callback;
  using tcp = Tcp;

  enum class LifecycleState { kIdle, kStarting, kStreaming, kDone };
  enum class TerminationState { kNone, kEof, kFailure, kCancelled };
  enum class CancellationState { kNone, kRequested };
  enum class NextReadState { kIdle, kPending };

  Impl(HttpSseClientTask::Executor io_executor,
       HttpSseClientTask::Executor callback_executor, std::string host,
       std::string port, std::string target, HttpSseClientOptions options,
       bool use_ssl, SslContextPtr ssl_ctx = {});

  HttpRequest& Request() noexcept;
  void Start(Callback cb);
  void Next(Callback cb);
  void Cancel();
  bool Failed() const noexcept;
  boost::system::error_code ErrorCode() const noexcept;
  HttpSseClientErrorStage ErrorStage() const noexcept;
  void SetCreateError(boost::system::error_code ec,
                      HttpSseClientErrorStage error_stage);
  void SetRawTcpStream(TcpStream stream);
  void SetRawSslStream(SslStream stream);

  /** @brief Set assembler and builder for assembled mode. */
  void SetAssembler(std::shared_ptr<RequestAssembler> assembler,
                    std::shared_ptr<StreamBuilder> builder);

  void DispatchCallback(Callback cb, HttpSseClientResult result) const;

 private:
  static void RunPostedStart(const std::shared_ptr<Impl>& self, Callback cb);
  static void RunPostedNext(const std::shared_ptr<Impl>& self, Callback cb);
  static void RunPostedCancel(const std::shared_ptr<Impl>& self);

  void DoStart();
  void DoAcquire();
  void OnAcquireComplete(boost::system::error_code ec, StreamSlot slot);
  void DoWriteRequest();
  void OnWriteRequest(boost::system::error_code ec);
  void OnReadHeader(boost::system::error_code ec);
  void DoReadNextChunk();
  void OnReadNextChunk(boost::system::error_code ec);
  /**
   * @brief Terminate SSE read with a final result and dispatch callback.
   *
   * Call chain: OnReadNextChunk → TerminateSseRead
   *
   * Handles all terminal states: cancellation, EOF, failure, and success.
   */
  void TerminateSseRead(HttpSseClientResult result);
  void FailStart(HttpSseClientErrorStage error_stage,
                 boost::system::error_code ec);
  void DoCancel();

  HttpSseClientTask::Executor io_executor_;
  HttpSseClientTask::Executor callback_executor_;
  boost::asio::strand<HttpSseClientTask::Executor> strand_;

  ClientStream stream_;

  FlatBuffer buffer_;
  std::optional<http_sse_detail::http::response_parser<
      http_sse_detail::http::string_body>>
      parser_;
  HttpRequest request_;

  std::string host_;
  std::string port_;
  std::string target_;
  HttpSseClientOptions options_;

  bool use_ssl_{false};
  SslContextPtr ssl_ctx_;

  LifecycleState lifecycle_state_{LifecycleState::kIdle};
  TerminationState termination_state_{TerminationState::kNone};
  CancellationState cancellation_state_{CancellationState::kNone};
  NextReadState next_read_state_{NextReadState::kIdle};
  std::size_t last_emitted_body_size_{0};

  std::optional<boost::system::error_code> create_error_;
  HttpSseClientErrorStage create_error_stage_{HttpSseClientErrorStage::kNone};

  boost::system::error_code error_code_;
  HttpSseClientErrorStage error_stage_{HttpSseClientErrorStage::kNone};

  Callback start_callback_;
  Callback next_callback_;

  // Assembler + builder (nullptr for raw mode).
  std::shared_ptr<RequestAssembler> assembler_;
  std::shared_ptr<StreamBuilder> builder_;
  ConnectionKey connection_key_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_IMPL_HTTP_SSE_CLIENT_TASK_IMPL_H_
