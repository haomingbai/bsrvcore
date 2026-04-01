/**
 * @file http_client_task_flow.cc
 * @brief Async transport flow for HttpClientTask::Impl.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <utility>

#include "bsrvcore/connection/client/http_client_session.h"
#include "impl/http_client_task_impl.h"

namespace bsrvcore {

namespace http = http_client_detail::http;

HttpClientTask::Impl::Impl(boost::asio::any_io_executor executor,
                           std::string host, std::string port,
                           std::string target,
                           http_client_detail::http::verb method,
                           HttpClientOptions options, bool use_ssl,
                           boost::asio::ssl::context* ssl_ctx)
    : executor_(std::move(executor)),
      strand_(executor_),
      resolver_(executor_),
      host_(std::move(host)),
      port_(std::move(port)),
      target_(std::move(target)),
      options_(std::move(options)),
      use_ssl_(use_ssl),
      ssl_ctx_(ssl_ctx) {
  request_.method(method);
  request_.target(target_);
  request_.version(11);
  request_.set(http::field::host, host_);
  if (!options_.user_agent.empty()) {
    request_.set(http::field::user_agent, options_.user_agent);
  }
  request_.keep_alive(options_.keep_alive);
}

HttpClientRequest& HttpClientTask::Impl::Request() noexcept { return request_; }

void HttpClientTask::Impl::Start() {
  auto self = shared_from_this();
  // The outer post only transfers control onto the strand. The posted helper
  // then owns the rest of the request pipeline.
  boost::asio::post(strand_,
                    [self = std::move(self)]() { RunPostedStart(self); });
}

void HttpClientTask::Impl::Cancel() {
  auto self = shared_from_this();
  // Cancellation follows the same explicit hop so shutdown cannot race with
  // other strand-owned state transitions.
  boost::asio::post(strand_,
                    [self = std::move(self)]() { RunPostedCancel(self); });
}

bool HttpClientTask::Impl::Failed() const noexcept { return failed_; }

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

void HttpClientTask::Impl::RunPostedStart(std::shared_ptr<Impl> self) {
  self->DoStart();
}

void HttpClientTask::Impl::RunPostedCancel(std::shared_ptr<Impl> self) {
  self->DoCancel();
}

void HttpClientTask::Impl::DoStart() {
  if (started_ || done_) {
    return;
  }
  started_ = true;

  if (create_error_) {
    Fail(create_error_stage_, *create_error_);
    return;
  }

  if (use_ssl_ && ssl_ctx_ == nullptr) {
    Fail(HttpClientErrorStage::kCreate,
         make_error_code(boost::system::errc::invalid_argument));
    return;
  }

  if (request_.find(http::field::host) == request_.end()) {
    request_.set(http::field::host, host_);
  }
  if (request_.find(http::field::user_agent) == request_.end() &&
      !options_.user_agent.empty()) {
    request_.set(http::field::user_agent, options_.user_agent);
  }
  request_.keep_alive(options_.keep_alive);

  {
    std::weak_ptr<HttpClientSession> weak;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      weak = session_;
    }
    if (auto session = weak.lock()) {
      // Session-level cookies are injected after callers finish mutating the
      // request but before prepare_payload() computes final framing headers.
      session->MaybeInjectCookies(request_, host_, target_, use_ssl_);
    }
  }

  request_.prepare_payload();
  resolver_.async_resolve(
      host_, port_,
      boost::asio::bind_executor(
          strand_,
          [self = shared_from_this()](boost::system::error_code ec,
                                      tcp::resolver::results_type results) {
            self->OnResolve(ec, std::move(results));
          }));
}

void HttpClientTask::Impl::OnResolve(boost::system::error_code ec,
                                     tcp::resolver::results_type results) {
  if (ec) {
    Fail(HttpClientErrorStage::kResolve, ec);
    return;
  }

  if (use_ssl_) {
    // The SSL and plain TCP branches intentionally stay structurally parallel
    // so stage-specific timeout/error reporting remains easy to audit.
    ssl_stream_.emplace(executor_, *ssl_ctx_);
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

  tcp_stream_.emplace(executor_);
  tcp_stream_->expires_after(options_.connect_timeout);
  tcp_stream_->async_connect(
      results,
      boost::asio::bind_executor(
          strand_, [self = shared_from_this()](
                       boost::system::error_code conn_ec,
                       const tcp::endpoint&) { self->OnConnect(conn_ec); }));
}

void HttpClientTask::Impl::OnConnect(boost::system::error_code ec) {
  if (ec) {
    Fail(HttpClientErrorStage::kConnect, ec);
    return;
  }

  if (use_ssl_) {
    if (SSL_set_tlsext_host_name(ssl_stream_->native_handle(), host_.c_str()) !=
        1) {
      boost::system::error_code sni_ec{static_cast<int>(::ERR_get_error()),
                                       boost::asio::error::get_ssl_category()};
      Fail(HttpClientErrorStage::kTlsHandshake, sni_ec);
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

  EmitConnected(boost::system::error_code{});
  DoWriteRequest();
}

void HttpClientTask::Impl::OnHandshake(boost::system::error_code ec) {
  if (ec) {
    Fail(HttpClientErrorStage::kTlsHandshake, ec);
    return;
  }

  // Once transport setup completes, the rest of the flow is identical to the
  // plain HTTP path: emit the stage transition, then write the prepared
  // request.
  EmitConnected(boost::system::error_code{});
  DoWriteRequest();
}

void HttpClientTask::Impl::DoWriteRequest() {
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

void HttpClientTask::Impl::OnWriteRequest(boost::system::error_code ec) {
  if (ec) {
    Fail(HttpClientErrorStage::kWriteRequest, ec);
    return;
  }

  parser_.emplace();
  parser_->body_limit(options_.max_response_body_bytes);

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

void HttpClientTask::Impl::OnReadHeader(boost::system::error_code ec) {
  if (ec) {
    Fail(HttpClientErrorStage::kReadHeader, ec);
    return;
  }

  {
    std::weak_ptr<HttpClientSession> weak;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      weak = session_;
    }
    if (auto session = weak.lock()) {
      const auto& base = parser_->get().base();
      auto range = base.equal_range(boost::beast::http::field::set_cookie);
      for (auto it = range.first; it != range.second; ++it) {
        session->SyncSetCookie(host_, target_, it->value());
      }
    }
  }

  HttpResponseHeader header;
  const auto& msg = parser_->get();
  header.version(msg.version());
  header.result(msg.result());
  for (const auto& f : msg.base()) {
    header.set(f.name_string(), f.value());
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
  if (use_ssl_) {
    boost::beast::get_lowest_layer(*ssl_stream_)
        .expires_after(options_.read_body_timeout);
    http::async_read(
        *ssl_stream_, buffer_, *parser_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 std::size_t) {
              self->OnReadBodyAll(ec);
            }));
    return;
  }

  tcp_stream_->expires_after(options_.read_body_timeout);
  http::async_read(*tcp_stream_, buffer_, *parser_,
                   boost::asio::bind_executor(
                       strand_, [self = shared_from_this()](
                                    boost::system::error_code ec, std::size_t) {
                         self->OnReadBodyAll(ec);
                       }));
}

void HttpClientTask::Impl::OnReadBodyAll(boost::system::error_code ec) {
  if (ec) {
    Fail(HttpClientErrorStage::kReadBody, ec);
    return;
  }

  auto response = parser_->release();
  Succeed(std::move(response));
}

void HttpClientTask::Impl::DoReadBodySome() {
  if (use_ssl_) {
    boost::beast::get_lowest_layer(*ssl_stream_)
        .expires_after(options_.read_body_timeout);
    http::async_read_some(
        *ssl_stream_, buffer_, *parser_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 std::size_t) {
              self->OnReadBodySome(ec);
            }));
    return;
  }

  tcp_stream_->expires_after(options_.read_body_timeout);
  http::async_read_some(
      *tcp_stream_, buffer_, *parser_,
      boost::asio::bind_executor(
          strand_, [self = shared_from_this()](boost::system::error_code ec,
                                               std::size_t) {
            self->OnReadBodySome(ec);
          }));
}

void HttpClientTask::Impl::OnReadBodySome(boost::system::error_code ec) {
  if (ec) {
    if (ec == http_client_detail::http::error::need_buffer &&
        parser_->is_done()) {
      ec = {};
    } else {
      Fail(HttpClientErrorStage::kReadBody, ec);
      return;
    }
  }

  const auto& body = parser_->get().body();
  if (body.size() > last_emitted_body_size_) {
    EmitChunk(body.substr(last_emitted_body_size_));
    last_emitted_body_size_ = body.size();
  }

  if (parser_->is_done()) {
    auto response = parser_->release();
    Succeed(std::move(response));
    return;
  }

  DoReadBodySome();
}

void HttpClientTask::Impl::DoCancel() {
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

void HttpClientTask::Impl::Fail(HttpClientErrorStage error_stage,
                                boost::system::error_code ec) {
  if (done_) {
    return;
  }

  failed_ = true;
  error_stage_ = error_stage;
  error_code_ = ec;

  HttpClientResult stage_result;
  stage_result.ec = ec;
  stage_result.cancelled =
      cancelled_ || (ec == boost::asio::error::operation_aborted);
  stage_result.error_stage = error_stage;
  stage_result.stage = ErrorStageToCallbackStage(error_stage);
  EmitStageByResult(stage_result);

  auto done_result = stage_result;
  done_result.stage = HttpClientStage::kDone;
  EmitDone(done_result);
  done_ = true;
}

void HttpClientTask::Impl::Succeed(HttpClientResponse response) {
  if (done_) {
    return;
  }

  HttpClientResult done_result;
  done_result.stage = HttpClientStage::kDone;
  done_result.error_stage = HttpClientErrorStage::kNone;
  done_result.cancelled = cancelled_;
  done_result.response = std::move(response);
  done_result.header = done_result.response.base();
  EmitDone(done_result);
  done_ = true;

  if (!options_.keep_alive) {
    DoCancel();
  }
}

}  // namespace bsrvcore
