/**
 * @file http_client_task_impl.h
 * @brief Internal HttpClientTask implementation declaration.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_IMPL_HTTP_CLIENT_TASK_IMPL_H_
#define BSRVCORE_CONNECTION_CLIENT_IMPL_HTTP_CLIENT_TASK_IMPL_H_

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "bsrvcore/connection/client/http_client_task.h"

namespace bsrvcore {

namespace http_client_detail {
namespace http = boost::beast::http;
}  // namespace http_client_detail

class HttpClientTask::Impl
    : public std::enable_shared_from_this<HttpClientTask::Impl> {
 public:
  using Callback = HttpClientTask::Callback;
  using tcp = Tcp;

  enum class LifecycleState { kIdle, kRunning, kDone };
  enum class CompletionState { kNone, kSuccess, kFailure, kCancelled };
  enum class CancellationState { kNone, kRequested };

  Impl(HttpClientTask::Executor io_executor,
       HttpClientTask::Executor callback_executor, std::string host,
       std::string port, std::string target,
       http_client_detail::http::verb method, HttpClientOptions options,
       bool use_ssl, SslContextPtr ssl_ctx = {});

  HttpClientRequest& Request() noexcept;
  void SetSession(std::weak_ptr<HttpClientSession> session);
  void SetOnConnected(Callback cb);
  void SetOnHeader(Callback cb);
  void SetOnChunk(Callback cb);
  void SetOnDone(Callback cb);

  void Start();
  void Cancel();
  bool Failed() const noexcept;
  boost::system::error_code ErrorCode() const noexcept;
  HttpClientErrorStage ErrorStage() const noexcept;
  void SetCreateError(boost::system::error_code ec,
                      HttpClientErrorStage error_stage);
  void SetRawTcpStream(TcpStream stream);
  void SetRawSslStream(SslStream stream);

 private:
  static void RunPostedStart(const std::shared_ptr<Impl>& self);
  static void RunPostedCancel(const std::shared_ptr<Impl>& self);

  void DoStart();
  void OnResolve(boost::system::error_code ec,
                 const tcp::resolver::results_type& results);
  void OnConnect(boost::system::error_code ec);
  void OnHandshake(boost::system::error_code ec);
  void DoWriteRequest();
  void OnWriteRequest(boost::system::error_code ec);
  void OnReadHeader(boost::system::error_code ec);
  void DoReadBodyAll();
  void OnReadBodyAll(boost::system::error_code ec);
  void DoReadBodySome();
  void OnReadBodySome(boost::system::error_code ec);
  void DoCancel();
  void CloseTransports();
  void Fail(HttpClientErrorStage error_stage, boost::system::error_code ec);
  void Succeed(HttpClientResponse response);

  void EmitConnected(boost::system::error_code ec);
  void EmitHeader(const HttpResponseHeader& header,
                  boost::system::error_code ec);
  void EmitChunk(std::string chunk);
  void EmitDone(const HttpClientResult& result);
  void EmitStageByResult(const HttpClientResult& result);
  bool HasChunkCallback() const;
  Callback GetCallbackCopy(HttpClientStage stage) const;
  Callback GetDoneCallbackCopy() const;
  void DispatchCallback(Callback cb, HttpClientResult result) const;
  static HttpClientStage ErrorStageToCallbackStage(
      HttpClientErrorStage error_stage);

  HttpClientTask::Executor io_executor_;
  HttpClientTask::Executor callback_executor_;
  boost::asio::strand<HttpClientTask::Executor> strand_;
  tcp::resolver resolver_;

  std::unique_ptr<TcpStream> tcp_stream_;
  std::unique_ptr<SslStream> ssl_stream_;

  FlatBuffer buffer_;
  std::optional<http_client_detail::http::response_parser<
      http_client_detail::http::string_body>>
      parser_;
  HttpClientRequest request_;

  std::string host_;
  std::string port_;
  std::string target_;
  HttpClientOptions options_;

  bool use_ssl_{false};
  SslContextPtr ssl_ctx_;
  bool raw_mode_{false};
  std::unique_ptr<TcpStream> raw_tcp_stream_;
  std::unique_ptr<SslStream> raw_ssl_stream_;

  LifecycleState lifecycle_state_{LifecycleState::kIdle};
  CompletionState completion_state_{CompletionState::kNone};
  CancellationState cancellation_state_{CancellationState::kNone};
  std::size_t last_emitted_body_size_{0};

  std::optional<boost::system::error_code> create_error_;
  HttpClientErrorStage create_error_stage_{HttpClientErrorStage::kNone};

  boost::system::error_code error_code_;
  HttpClientErrorStage error_stage_{HttpClientErrorStage::kNone};

  mutable std::mutex callback_mutex_;
  Callback on_connected_;
  Callback on_header_;
  Callback on_chunk_;
  Callback on_done_;

  std::weak_ptr<HttpClientSession> session_;
};

}  // namespace bsrvcore

#endif
