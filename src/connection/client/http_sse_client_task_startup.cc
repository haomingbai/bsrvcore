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
                              bool use_ssl, boost::asio::ssl::context* ssl_ctx)
    : io_executor_(std::move(io_executor)),
      callback_executor_(std::move(callback_executor)),
      strand_(io_executor_),
      resolver_(io_executor_),
      host_(std::move(host)),
      port_(std::move(port)),
      target_(std::move(target)),
      options_(std::move(options)),
      use_ssl_(use_ssl),
      ssl_ctx_(ssl_ctx) {
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

bool HttpSseClientTask::Impl::Failed() const noexcept { return failed_; }

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
  if (started_ || done_) {
    return;
  }
  started_ = true;

  if (create_error_) {
    FailStart(create_error_stage_, *create_error_);
    return;
  }

  if (use_ssl_ && ssl_ctx_ == nullptr) {
    FailStart(HttpSseClientErrorStage::kCreate,
              make_error_code(boost::system::errc::invalid_argument));
    return;
  }

  if (request_.find(http::field::host) == request_.end()) {
    request_.set(http::field::host, host_);
  }
  if (request_.find(http::field::accept) == request_.end()) {
    request_.set(http::field::accept, "text/event-stream");
  }
  request_.keep_alive(options_.keep_alive);

  resolver_.async_resolve(
      host_, port_,
      boost::asio::bind_executor(
          strand_,
          [self = shared_from_this()](boost::system::error_code ec,
                                      tcp::resolver::results_type results) {
            self->OnResolve(ec, std::move(results));
          }));
}

void HttpSseClientTask::Impl::OnResolve(
    boost::system::error_code ec, const tcp::resolver::results_type& results) {
  if (ec) {
    FailStart(HttpSseClientErrorStage::kResolve, ec);
    return;
  }

  if (use_ssl_) {
    ssl_stream_.emplace(io_executor_, *ssl_ctx_);
    boost::beast::get_lowest_layer(*ssl_stream_)
        .expires_after(options_.connect_timeout);
    boost::beast::get_lowest_layer(*ssl_stream_)
        .async_connect(results,
                       boost::asio::bind_executor(
                           strand_, [self = shared_from_this()](
                                        boost::system::error_code conn_ec,
                                        const tcp::endpoint&) {
                             self->OnConnect(conn_ec);
                           }));
    return;
  }

  tcp_stream_.emplace(io_executor_);
  tcp_stream_->expires_after(options_.connect_timeout);
  tcp_stream_->async_connect(
      results,
      boost::asio::bind_executor(
          strand_, [self = shared_from_this()](
                       boost::system::error_code conn_ec,
                       const tcp::endpoint&) { self->OnConnect(conn_ec); }));
}

void HttpSseClientTask::Impl::OnConnect(boost::system::error_code ec) {
  if (ec) {
    FailStart(HttpSseClientErrorStage::kConnect, ec);
    return;
  }

  if (use_ssl_) {
    if (SSL_set_tlsext_host_name(ssl_stream_->native_handle(), host_.c_str()) !=
        1) {
      boost::system::error_code const sni_ec{
          static_cast<int>(::ERR_get_error()),
          boost::asio::error::get_ssl_category()};
      FailStart(HttpSseClientErrorStage::kTlsHandshake, sni_ec);
      return;
    }

    if (options_.verify_peer) {
      ssl_stream_->set_verify_mode(boost::asio::ssl::verify_peer);
      ssl_stream_->set_verify_callback(
          boost::asio::ssl::host_name_verification(host_));
    } else {
      ssl_stream_->set_verify_mode(boost::asio::ssl::verify_none);
    }

    boost::beast::get_lowest_layer(*ssl_stream_)
        .expires_after(options_.tls_handshake_timeout);
    ssl_stream_->async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec) {
              self->OnHandshake(ec);
            }));
    return;
  }

  DoWriteRequest();
}

void HttpSseClientTask::Impl::OnHandshake(boost::system::error_code ec) {
  if (ec) {
    FailStart(HttpSseClientErrorStage::kTlsHandshake, ec);
    return;
  }

  DoWriteRequest();
}

void HttpSseClientTask::Impl::DoWriteRequest() {
  request_.prepare_payload();

  if (use_ssl_) {
    boost::beast::get_lowest_layer(*ssl_stream_)
        .expires_after(options_.write_timeout);
    http::async_write(
        *ssl_stream_, request_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 std::size_t) {
              self->OnWriteRequest(ec);
            }));
    return;
  }

  tcp_stream_->expires_after(options_.write_timeout);
  http::async_write(
      *tcp_stream_, request_,
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
  if (use_ssl_) {
    boost::beast::get_lowest_layer(*ssl_stream_)
        .expires_after(options_.read_header_timeout);
    http::async_read_header(
        *ssl_stream_, buffer_, *parser_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 std::size_t) {
              self->OnReadHeader(ec);
            }));
    return;
  }

  tcp_stream_->expires_after(options_.read_header_timeout);
  http::async_read_header(
      *tcp_stream_, buffer_, *parser_,
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

  HttpSseClientResult result;
  result.stage = HttpSseClientStage::kStart;
  result.error_stage = HttpSseClientErrorStage::kNone;
  result.header = msg.base();
  Callback const callback = std::move(start_callback_);
  DispatchCallback(callback, std::move(result));
}

void HttpSseClientTask::Impl::FailStart(HttpSseClientErrorStage error_stage,
                                        boost::system::error_code ec) {
  if (done_) {
    return;
  }

  failed_ = true;
  error_stage_ = error_stage;
  error_code_ = ec;

  HttpSseClientResult result;
  result.stage = HttpSseClientStage::kStart;
  result.error_stage = error_stage;
  result.ec = ec;
  result.cancelled =
      cancelled_ || (ec == boost::asio::error::operation_aborted);

  done_ = true;

  Callback const callback = std::move(start_callback_);
  DispatchCallback(callback, std::move(result));
}

void HttpSseClientTask::Impl::DoCancel() {
  if (done_) {
    return;
  }

  cancelled_ = true;
  resolver_.cancel();

  boost::system::error_code ignored;
  if (tcp_stream_) {
    tcp_stream_->socket().cancel(ignored);
    tcp_stream_->socket().shutdown(tcp::socket::shutdown_both, ignored);
    tcp_stream_->socket().close(ignored);
  }
  if (ssl_stream_) {
    auto& socket = boost::beast::get_lowest_layer(*ssl_stream_).socket();
    socket.cancel(ignored);
    socket.shutdown(tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
  }
}

}  // namespace bsrvcore
