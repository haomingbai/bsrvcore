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

#include <boost/asio/any_io_executor.hpp>
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
  using tcp = boost::asio::ip::tcp;

  Impl(boost::asio::any_io_executor executor, std::string host,
       std::string port, std::string target, HttpSseClientOptions options,
       bool use_ssl, boost::asio::ssl::context* ssl_ctx);

  HttpRequest& Request() noexcept;
  void Start(Callback cb);
  void Next(Callback cb);
  void Cancel();
  bool Failed() const noexcept;
  boost::system::error_code ErrorCode() const noexcept;
  HttpSseClientErrorStage ErrorStage() const noexcept;
  void SetCreateError(boost::system::error_code ec,
                      HttpSseClientErrorStage error_stage);

 private:
  static void RunPostedStart(std::shared_ptr<Impl> self, Callback cb);
  static void RunPostedNext(std::shared_ptr<Impl> self, Callback cb);
  static void RunPostedCancel(std::shared_ptr<Impl> self);

  void DoStart();
  void OnResolve(boost::system::error_code ec,
                 tcp::resolver::results_type results);
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

  boost::asio::any_io_executor executor_;
  boost::asio::strand<boost::asio::any_io_executor> strand_;
  tcp::resolver resolver_;

  std::optional<boost::beast::tcp_stream> tcp_stream_;
  std::optional<boost::beast::ssl_stream<boost::beast::tcp_stream>> ssl_stream_;

  boost::beast::flat_buffer buffer_;
  std::optional<http_sse_detail::http::response_parser<
      http_sse_detail::http::string_body>>
      parser_;
  HttpRequest request_;

  std::string host_;
  std::string port_;
  std::string target_;
  HttpSseClientOptions options_;

  bool use_ssl_{false};
  boost::asio::ssl::context* ssl_ctx_{nullptr};

  bool started_{false};
  bool done_{false};
  bool cancelled_{false};
  bool next_pending_{false};
  bool failed_{false};
  std::size_t last_emitted_body_size_{0};

  std::optional<boost::system::error_code> create_error_{};
  HttpSseClientErrorStage create_error_stage_{HttpSseClientErrorStage::kNone};

  boost::system::error_code error_code_{};
  HttpSseClientErrorStage error_stage_{HttpSseClientErrorStage::kNone};

  Callback start_callback_{};
  Callback next_callback_{};
};

}  // namespace bsrvcore

#endif
