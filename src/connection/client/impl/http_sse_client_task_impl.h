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

namespace bsrvcore {

namespace http_sse_detail {
namespace http = boost::beast::http;
}  // namespace http_sse_detail

class HttpSseClientTask::Impl
    : public std::enable_shared_from_this<HttpSseClientTask::Impl> {
 public:
  using Callback = HttpSseClientTask::Callback;
  using tcp = Tcp;

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
  void DispatchCallback(Callback cb, HttpSseClientResult result) const;

 private:
  static void RunPostedStart(const std::shared_ptr<Impl>& self, Callback cb);
  static void RunPostedNext(const std::shared_ptr<Impl>& self, Callback cb);
  static void RunPostedCancel(const std::shared_ptr<Impl>& self);

  void DoStart();
  void OnResolve(boost::system::error_code ec,
                 const tcp::resolver::results_type& results);
  void OnConnect(boost::system::error_code ec);
  void OnHandshake(boost::system::error_code ec);
  void DoWriteRequest();
  void OnWriteRequest(boost::system::error_code ec);
  void OnReadHeader(boost::system::error_code ec);
  void DoReadNextChunk();
  void OnReadNextChunk(boost::system::error_code ec);
  void FailStart(HttpSseClientErrorStage error_stage,
                 boost::system::error_code ec);
  void DoCancel();

  HttpSseClientTask::Executor io_executor_;
  HttpSseClientTask::Executor callback_executor_;
  boost::asio::strand<HttpSseClientTask::Executor> strand_;
  tcp::resolver resolver_;

  std::unique_ptr<TcpStream> tcp_stream_;
  std::unique_ptr<SslStream> ssl_stream_;

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

  bool started_{false};
  bool done_{false};
  bool cancelled_{false};
  bool next_pending_{false};
  bool failed_{false};
  std::size_t last_emitted_body_size_{0};

  std::optional<boost::system::error_code> create_error_;
  HttpSseClientErrorStage create_error_stage_{HttpSseClientErrorStage::kNone};

  boost::system::error_code error_code_;
  HttpSseClientErrorStage error_stage_{HttpSseClientErrorStage::kNone};

  Callback start_callback_;
  Callback next_callback_;
};

}  // namespace bsrvcore

#endif
