/**
 * @file http_sse_client_task.cc
 * @brief Implementation of asynchronous SSE client task.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/client/http_sse_client_task.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <cctype>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "impl/http_url_parser.h"

namespace bsrvcore {

namespace {

namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;

using connection_internal::ParsedUrl;
using connection_internal::ParseHttpUrl;

std::string ToLower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

}  // namespace

class HttpSseClientTask::Impl
    : public std::enable_shared_from_this<HttpSseClientTask::Impl> {
 public:
  Impl(boost::asio::any_io_executor executor, std::string host,
       std::string port, std::string target, HttpSseClientOptions options,
       bool use_ssl, boost::asio::ssl::context* ssl_ctx)
      : executor_(std::move(executor)),
        strand_(executor_),
        resolver_(executor_),
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

  HttpRequest& Request() noexcept { return request_; }

  void Start(Callback cb) {
    auto self = shared_from_this();
    // Serialize task state changes onto the strand.
    boost::asio::post(strand_, [self, cb = std::move(cb)]() mutable {
      self->start_callback_ = std::move(cb);
      self->DoStart();
    });
  }

  void Next(Callback cb) {
    auto self = shared_from_this();
    // Next() is a pull model: at most one Next() can be in-flight.
    boost::asio::post(strand_, [self, cb = std::move(cb)]() mutable {
      if (!self->started_ || self->done_) {
        // Next() is only valid after Start() succeeded and before completion.
        HttpSseClientResult result;
        result.stage = HttpSseClientStage::kNext;
        result.error_stage = HttpSseClientErrorStage::kReadBody;
        result.ec =
            make_error_code(boost::system::errc::operation_not_permitted);
        cb(result);
        return;
      }

      if (self->next_pending_) {
        // Protect against overlapping reads: the body buffer grows
        // monotonically.
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
    });
  }

  void Cancel() {
    auto self = shared_from_this();
    // Cancellation is best-effort and closes underlying transport.
    boost::asio::post(strand_, [self]() { self->DoCancel(); });
  }

  bool Failed() const noexcept { return failed_; }
  boost::system::error_code ErrorCode() const noexcept { return error_code_; }
  HttpSseClientErrorStage ErrorStage() const noexcept { return error_stage_; }

  void SetCreateError(boost::system::error_code ec,
                      HttpSseClientErrorStage error_stage) {
    create_error_ = ec;
    create_error_stage_ = error_stage;
  }

 private:
  void DoStart() {
    if (started_ || done_) {
      // Idempotent start: ignore repeated calls.
      return;
    }
    started_ = true;

    if (create_error_) {
      FailStart(create_error_stage_, *create_error_);
      return;
    }

    if (use_ssl_ && ssl_ctx_ == nullptr) {
      // HTTPS requires an SSL context; without it we fail fast.
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

    // Kick off: resolve -> connect -> (optional TLS) -> write -> read header.
    resolver_.async_resolve(
        host_, port_,
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this()](boost::system::error_code ec,
                                        tcp::resolver::results_type results) {
              self->OnResolve(ec, std::move(results));
            }));
  }

  void OnResolve(boost::system::error_code ec,
                 tcp::resolver::results_type results) {
    if (ec) {
      FailStart(HttpSseClientErrorStage::kResolve, ec);
      return;
    }

    // Construct the appropriate stream type, then async_connect.
    if (use_ssl_) {
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

  void OnConnect(boost::system::error_code ec) {
    if (ec) {
      FailStart(HttpSseClientErrorStage::kConnect, ec);
      return;
    }

    if (use_ssl_) {
      // For TLS, set SNI first so the server can pick the right certificate.
      if (SSL_set_tlsext_host_name(ssl_stream_->native_handle(),
                                   host_.c_str()) != 1) {
        boost::system::error_code sni_ec{
            static_cast<int>(::ERR_get_error()),
            boost::asio::error::get_ssl_category()};
        FailStart(HttpSseClientErrorStage::kTlsHandshake, sni_ec);
        return;
      }

      // Peer verification is optional to support self-signed endpoints in
      // tests.
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
              strand_,
              [self = shared_from_this()](boost::system::error_code hs_ec) {
                self->OnHandshake(hs_ec);
              }));
      return;
    }

    // Plain TCP connected.
    DoWriteRequest();
  }

  void OnHandshake(boost::system::error_code ec) {
    if (ec) {
      FailStart(HttpSseClientErrorStage::kTlsHandshake, ec);
      return;
    }

    // TLS handshake complete.
    DoWriteRequest();
  }

  void DoWriteRequest() {
    // Prepare and write the SSE GET request.
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

  void OnWriteRequest(boost::system::error_code ec) {
    if (ec) {
      FailStart(HttpSseClientErrorStage::kWriteRequest, ec);
      return;
    }

    // Read header first to validate SSE response properties before streaming.
    parser_.emplace();

    if (use_ssl_) {
      boost::beast::get_lowest_layer(*ssl_stream_)
          .expires_after(options_.read_header_timeout);
      http::async_read_header(
          *ssl_stream_, buffer_, *parser_,
          boost::asio::bind_executor(
              strand_, [self = shared_from_this()](
                           boost::system::error_code hdr_ec, std::size_t) {
                self->OnReadHeader(hdr_ec);
              }));
      return;
    }

    tcp_stream_->expires_after(options_.read_header_timeout);
    http::async_read_header(
        *tcp_stream_, buffer_, *parser_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](
                         boost::system::error_code hdr_ec, std::size_t) {
              self->OnReadHeader(hdr_ec);
            }));
  }

  void OnReadHeader(boost::system::error_code ec) {
    if (ec) {
      FailStart(HttpSseClientErrorStage::kReadHeader, ec);
      return;
    }

    const auto& msg = parser_->get();
    // Minimal validation: SSE must respond with 200 OK and event-stream
    // content-type.
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

    if (start_callback_) {
      start_callback_(result);
    }
  }

  void DoReadNextChunk() {
    // Pull one incremental body read. The parser keeps an internal growing body
    // string.
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

  void OnReadNextChunk(boost::system::error_code ec) {
    HttpSseClientResult result;
    result.stage = HttpSseClientStage::kNext;

    if (ec) {
      // Cancellation path: expose the cancellation and converge task.
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

      // Normal stream end / remote close is surfaced as eof=true (not a
      // failure).
      if (ec == http::error::end_of_stream || ec == boost::asio::error::eof) {
        result.eof = true;
        next_pending_ = false;
        if (next_callback_) {
          next_callback_(result);
        }
        done_ = true;
        return;
      }

      // Transport error.
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

    // Body is an ever-growing string; only return the delta since last
    // callback.
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

  void FailStart(HttpSseClientErrorStage error_stage,
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
    // "Cancelled" is derived both from explicit Cancel() and asio's abort code.
    result.cancelled =
        cancelled_ || (ec == boost::asio::error::operation_aborted);

    if (start_callback_) {
      start_callback_(result);
    }
    done_ = true;
  }

  void DoCancel() {
    if (done_) {
      return;
    }

    // Cancellation is best-effort: cancel resolve and close sockets.
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

 private:
  boost::asio::any_io_executor executor_;
  boost::asio::strand<boost::asio::any_io_executor> strand_;
  tcp::resolver resolver_;

  std::optional<boost::beast::tcp_stream> tcp_stream_;
  std::optional<boost::beast::ssl_stream<boost::beast::tcp_stream>> ssl_stream_;

  boost::beast::flat_buffer buffer_;
  std::optional<http::response_parser<http::string_body>> parser_;
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

HttpSseClientTask::HttpSseClientTask(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

HttpSseClientTask::~HttpSseClientTask() = default;

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttp(
    boost::asio::any_io_executor executor, std::string host, std::string port,
    std::string target, HttpSseClientOptions options) {
  auto impl = AllocateShared<Impl>(std::move(executor), std::move(host),
                                   std::move(port), std::move(target),
                                   std::move(options), false, nullptr);
  void* raw = Allocate(sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
  try {
    auto* task = new (raw) HttpSseClientTask(std::move(impl));
    return std::shared_ptr<HttpSseClientTask>(
        task, [](HttpSseClientTask* ptr) { DestroyDeallocate(ptr); });
  } catch (...) {
    Deallocate(raw, sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
    throw;
  }
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateHttps(
    boost::asio::any_io_executor executor, boost::asio::ssl::context& ssl_ctx,
    std::string host, std::string port, std::string target,
    HttpSseClientOptions options) {
  auto impl = AllocateShared<Impl>(std::move(executor), std::move(host),
                                   std::move(port), std::move(target),
                                   std::move(options), true, &ssl_ctx);
  void* raw = Allocate(sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
  try {
    auto* task = new (raw) HttpSseClientTask(std::move(impl));
    return std::shared_ptr<HttpSseClientTask>(
        task, [](HttpSseClientTask* ptr) { DestroyDeallocate(ptr); });
  } catch (...) {
    Deallocate(raw, sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
    throw;
  }
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateFromUrl(
    boost::asio::any_io_executor executor, std::string url,
    HttpSseClientOptions options) {
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    auto impl = AllocateShared<Impl>(std::move(executor), "", "", "/",
                                     std::move(options), false, nullptr);
    impl->SetCreateError(make_error_code(boost::system::errc::invalid_argument),
                         HttpSseClientErrorStage::kCreate);
    void* raw = Allocate(sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
    try {
      auto* task = new (raw) HttpSseClientTask(std::move(impl));
      return std::shared_ptr<HttpSseClientTask>(
          task, [](HttpSseClientTask* ptr) { DestroyDeallocate(ptr); });
    } catch (...) {
      Deallocate(raw, sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
      throw;
    }
  }

  auto impl = AllocateShared<Impl>(std::move(executor), parsed->host,
                                   parsed->port, parsed->target,
                                   std::move(options), parsed->https, nullptr);

  if (parsed->https) {
    impl->SetCreateError(make_error_code(boost::system::errc::invalid_argument),
                         HttpSseClientErrorStage::kCreate);
  }

  void* raw = Allocate(sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
  try {
    auto* task = new (raw) HttpSseClientTask(std::move(impl));
    return std::shared_ptr<HttpSseClientTask>(
        task, [](HttpSseClientTask* ptr) { DestroyDeallocate(ptr); });
  } catch (...) {
    Deallocate(raw, sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
    throw;
  }
}

std::shared_ptr<HttpSseClientTask> HttpSseClientTask::CreateFromUrl(
    boost::asio::any_io_executor executor, boost::asio::ssl::context& ssl_ctx,
    std::string url, HttpSseClientOptions options) {
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    auto impl = AllocateShared<Impl>(std::move(executor), "", "", "/",
                                     std::move(options), false, nullptr);
    impl->SetCreateError(make_error_code(boost::system::errc::invalid_argument),
                         HttpSseClientErrorStage::kCreate);
    void* raw = Allocate(sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
    try {
      auto* task = new (raw) HttpSseClientTask(std::move(impl));
      return std::shared_ptr<HttpSseClientTask>(
          task, [](HttpSseClientTask* ptr) { DestroyDeallocate(ptr); });
    } catch (...) {
      Deallocate(raw, sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
      throw;
    }
  }

  auto impl = AllocateShared<Impl>(
      std::move(executor), parsed->host, parsed->port, parsed->target,
      std::move(options), parsed->https, parsed->https ? &ssl_ctx : nullptr);
  void* raw = Allocate(sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
  try {
    auto* task = new (raw) HttpSseClientTask(std::move(impl));
    return std::shared_ptr<HttpSseClientTask>(
        task, [](HttpSseClientTask* ptr) { DestroyDeallocate(ptr); });
  } catch (...) {
    Deallocate(raw, sizeof(HttpSseClientTask), alignof(HttpSseClientTask));
    throw;
  }
}

HttpRequest& HttpSseClientTask::Request() noexcept { return impl_->Request(); }

void HttpSseClientTask::Start(Callback cb) { impl_->Start(std::move(cb)); }

void HttpSseClientTask::Next(Callback cb) { impl_->Next(std::move(cb)); }

void HttpSseClientTask::Cancel() { impl_->Cancel(); }

bool HttpSseClientTask::Failed() const noexcept { return impl_->Failed(); }

boost::system::error_code HttpSseClientTask::ErrorCode() const noexcept {
  return impl_->ErrorCode();
}

HttpSseClientErrorStage HttpSseClientTask::ErrorStage() const noexcept {
  return impl_->ErrorStage();
}

}  // namespace bsrvcore
