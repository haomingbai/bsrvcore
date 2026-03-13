/**
 * @file http_client_task.cc
 * @brief Implementation of asynchronous HTTP/HTTPS client task.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/http_client_task.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/url.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace bsrvcore {

namespace {

namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;

struct ParsedUrl {
  bool https{false};
  std::string host;
  std::string port;
  std::string target;
};

std::optional<ParsedUrl> ParseHttpUrl(const std::string& url) {
  // Parse absolute URI like: http(s)://host[:port]/path?query.
  // We intentionally accept only http/https schemes here.
  auto parsed = boost::urls::parse_uri(url);
  if (!parsed) {
    return std::nullopt;
  }

  const auto& u = parsed.value();
  const auto scheme = u.scheme();
  if (scheme != "http" && scheme != "https") {
    return std::nullopt;
  }

  if (u.host().empty()) {
    return std::nullopt;
  }

  ParsedUrl out;
  out.https = (scheme == "https");
  out.host = std::string(u.host());
  // If port is absent, pick the scheme default.
  out.port = u.has_port() ? std::string(u.port()) : (out.https ? "443" : "80");

  // Rebuild request-target (path + optional query). Keep encoded bytes as-is.
  std::string target = std::string(u.encoded_path());
  if (target.empty()) {
    target = "/";
  }
  if (u.has_query()) {
    target.push_back('?');
    target.append(u.encoded_query());
  }
  out.target = std::move(target);

  return out;
}

}  // namespace

class HttpClientTask::Impl : public std::enable_shared_from_this<HttpClientTask::Impl> {
 public:
  Impl(boost::asio::any_io_executor executor, std::string host, std::string port,
       std::string target, http::verb method, HttpClientOptions options,
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
    request_.method(method);
    request_.target(target_);
    request_.version(11);
    request_.set(http::field::host, host_);
    if (!options_.user_agent.empty()) {
      request_.set(http::field::user_agent, options_.user_agent);
    }
    request_.keep_alive(options_.keep_alive);
  }

  HttpClientRequest& Request() noexcept { return request_; }

  void SetOnConnected(Callback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_connected_ = std::move(cb);
  }

  void SetOnHeader(Callback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_header_ = std::move(cb);
  }

  void SetOnChunk(Callback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_chunk_ = std::move(cb);
  }

  void SetOnDone(Callback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_done_ = std::move(cb);
  }

  void Start() {
    auto self = shared_from_this();
    // Serialize all state transitions onto the strand.
    boost::asio::post(strand_, [self]() { self->DoStart(); });
  }

  void Cancel() {
    auto self = shared_from_this();
    // Cancellation is also serialized onto the strand to avoid races.
    boost::asio::post(strand_, [self]() { self->DoCancel(); });
  }

  bool Failed() const noexcept { return failed_; }

  boost::system::error_code ErrorCode() const noexcept { return error_code_; }

  HttpClientErrorStage ErrorStage() const noexcept { return error_stage_; }

  void SetCreateError(boost::system::error_code ec,
                      HttpClientErrorStage error_stage) {
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
        // Creation-time validation failure (e.g. invalid URL).
      Fail(create_error_stage_, *create_error_);
      return;
    }

    if (use_ssl_ && ssl_ctx_ == nullptr) {
        // HTTPS requires an SSL context; without it we fail fast.
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
    // Ensure Content-Length/Transfer-Encoding is consistent with the body.
    request_.prepare_payload();

    // Kick off: resolve -> connect -> (optional TLS) -> write -> read header -> read body.
    resolver_.async_resolve(
        host_, port_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 tcp::resolver::results_type results) {
              self->OnResolve(ec, std::move(results));
            }));
  }

  void OnResolve(boost::system::error_code ec,
                 tcp::resolver::results_type results) {
    if (ec) {
      Fail(HttpClientErrorStage::kResolve, ec);
      return;
    }

    // Construct the appropriate stream type, then async_connect.
    // Note: we set per-operation deadlines via beast stream expires_after().
    if (use_ssl_) {
      ssl_stream_.emplace(executor_, *ssl_ctx_);
      boost::beast::get_lowest_layer(*ssl_stream_)
          .expires_after(options_.connect_timeout);
      boost::beast::get_lowest_layer(*ssl_stream_)
          .async_connect(results, boost::asio::bind_executor(
                                      strand_,
                                      [self = shared_from_this()](
                                          boost::system::error_code conn_ec,
                                          const tcp::endpoint&) {
                                        self->OnConnect(conn_ec);
                                      }));
      return;
    }

    tcp_stream_.emplace(executor_);
    tcp_stream_->expires_after(options_.connect_timeout);
    tcp_stream_->async_connect(
        results, boost::asio::bind_executor(
                     strand_,
                     [self = shared_from_this()](boost::system::error_code conn_ec,
                                                 const tcp::endpoint&) {
                       self->OnConnect(conn_ec);
                     }));
  }

  void OnConnect(boost::system::error_code ec) {
    if (ec) {
      Fail(HttpClientErrorStage::kConnect, ec);
      return;
    }

    if (use_ssl_) {
      // For TLS, set SNI first so the server can pick the right certificate.
      if (SSL_set_tlsext_host_name(ssl_stream_->native_handle(), host_.c_str()) !=
          1) {
        boost::system::error_code sni_ec{static_cast<int>(::ERR_get_error()),
                                         boost::asio::error::get_ssl_category()};
        Fail(HttpClientErrorStage::kTlsHandshake, sni_ec);
        return;
      }

      // Peer verification is optional to support self-signed endpoints in tests.
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
              strand_, [self = shared_from_this()](boost::system::error_code hs_ec) {
                self->OnHandshake(hs_ec);
              }));
      return;
    }

    // Plain TCP connected.
    EmitConnected(boost::system::error_code{});
    DoWriteRequest();
  }

  void OnHandshake(boost::system::error_code ec) {
    if (ec) {
      Fail(HttpClientErrorStage::kTlsHandshake, ec);
      return;
    }

    // TLS handshake complete.
    EmitConnected(boost::system::error_code{});
    DoWriteRequest();
  }

  void DoWriteRequest() {
    // Send request bytes. Errors here are classified as kWriteRequest.
    if (use_ssl_) {
      boost::beast::get_lowest_layer(*ssl_stream_).expires_after(
          options_.write_timeout);
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
      Fail(HttpClientErrorStage::kWriteRequest, ec);
      return;
    }

    // Use a response parser so we can set body size limits and support read_some.
    parser_.emplace();
    parser_->body_limit(options_.max_response_body_bytes);

    // Read header first so we can publish it early and decide the body read mode.
    if (use_ssl_) {
      boost::beast::get_lowest_layer(*ssl_stream_).expires_after(
          options_.read_header_timeout);
      http::async_read_header(
          *ssl_stream_, buffer_, *parser_,
          boost::asio::bind_executor(
              strand_, [self = shared_from_this()](boost::system::error_code hdr_ec,
                                                   std::size_t) {
                self->OnReadHeader(hdr_ec);
              }));
      return;
    }

    tcp_stream_->expires_after(options_.read_header_timeout);
    http::async_read_header(
        *tcp_stream_, buffer_, *parser_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code hdr_ec,
                                                 std::size_t) {
              self->OnReadHeader(hdr_ec);
            }));
  }

  void OnReadHeader(boost::system::error_code ec) {
    if (ec) {
      Fail(HttpClientErrorStage::kReadHeader, ec);
      return;
    }

    // Snapshot the header into a lightweight object for callbacks.
    HttpResponseHeader header;
    const auto& msg = parser_->get();
    header.version(msg.version());
    header.result(msg.result());
    for (const auto& f : msg.base()) {
      header.set(f.name(), f.value());
    }

    EmitHeader(header, boost::system::error_code{});

    if (HasChunkCallback()) {
      // Streaming mode: emit only newly appended body bytes on each read_some.
      last_emitted_body_size_ = parser_->get().body().size();
      DoReadBodySome();
      return;
    }

    // Non-streaming mode: read entire body then deliver final done result.
    DoReadBodyAll();
  }

  void DoReadBodyAll() {
    if (use_ssl_) {
      boost::beast::get_lowest_layer(*ssl_stream_).expires_after(
          options_.read_body_timeout);
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
    http::async_read(
        *tcp_stream_, buffer_, *parser_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::system::error_code ec,
                                                 std::size_t) {
              self->OnReadBodyAll(ec);
            }));
  }

  void OnReadBodyAll(boost::system::error_code ec) {
    if (ec) {
      Fail(HttpClientErrorStage::kReadBody, ec);
      return;
    }

    // Full response successfully assembled.
    auto response = parser_->release();
    Succeed(std::move(response));
  }

  void DoReadBodySome() {
    if (use_ssl_) {
      boost::beast::get_lowest_layer(*ssl_stream_).expires_after(
          options_.read_body_timeout);
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

  void OnReadBodySome(boost::system::error_code ec) {
    if (ec) {
      // Beast uses need_buffer as a hint that it needs more octets to continue;
      // treat it as success when the parser reports completion.
      if (ec == http::error::need_buffer && parser_->is_done()) {
        ec = {};
      } else {
        Fail(HttpClientErrorStage::kReadBody, ec);
        return;
      }
    }

    // Emit incremental bytes since last callback.
    const auto& body = parser_->get().body();
    if (body.size() > last_emitted_body_size_) {
      EmitChunk(body.substr(last_emitted_body_size_));
      last_emitted_body_size_ = body.size();
    }

    if (parser_->is_done()) {
      // Body complete; release parsed response and converge at Done.
      auto response = parser_->release();
      Succeed(std::move(response));
      return;
    }

    // Continue pulling more bytes.
    DoReadBodySome();
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

  void Fail(HttpClientErrorStage error_stage, boost::system::error_code ec) {
    if (done_) {
      return;
    }

    failed_ = true;
    error_stage_ = error_stage;
    error_code_ = ec;

  // "Cancelled" is derived both from explicit Cancel() and asio's abort code.
    const bool cancelled = cancelled_ ||
                           (ec == boost::asio::error::operation_aborted);

    HttpClientResult stage_result;
    stage_result.ec = ec;
    stage_result.cancelled = cancelled;
    stage_result.error_stage = error_stage;
    stage_result.stage = ErrorStageToCallbackStage(error_stage);
    EmitStageByResult(stage_result);

    HttpClientResult done_result = stage_result;
    done_result.stage = HttpClientStage::kDone;
    EmitDone(done_result);

    done_ = true;
  }

  void Succeed(HttpClientResponse response) {
    if (done_) {
      return;
    }

    // Success always converges at Done.
    HttpClientResult done_result;
    done_result.stage = HttpClientStage::kDone;
    done_result.error_stage = HttpClientErrorStage::kNone;
    done_result.cancelled = cancelled_;
    done_result.response = std::move(response);
    done_result.header = done_result.response.base();

    EmitDone(done_result);
    done_ = true;

    if (!options_.keep_alive) {
      // If keep-alive is disabled, proactively close the transport.
      DoCancel();
    }
  }

  void EmitConnected(boost::system::error_code ec) {
    HttpClientResult result;
    result.ec = ec;
    result.stage = HttpClientStage::kConnected;
    result.cancelled = cancelled_;
    result.error_stage = HttpClientErrorStage::kNone;

    auto cb = GetCallbackCopy(HttpClientStage::kConnected);
    if (cb) {
      cb(result);
    }
  }

  void EmitHeader(const HttpResponseHeader& header, boost::system::error_code ec) {
    HttpClientResult result;
    result.ec = ec;
    result.stage = HttpClientStage::kHeader;
    result.cancelled = cancelled_;
    result.error_stage = HttpClientErrorStage::kNone;
    result.header = header;

    auto cb = GetCallbackCopy(HttpClientStage::kHeader);
    if (cb) {
      cb(result);
    }
  }

  void EmitChunk(std::string chunk) {
    HttpClientResult result;
    result.stage = HttpClientStage::kChunk;
    result.cancelled = cancelled_;
    result.error_stage = HttpClientErrorStage::kNone;
    result.chunk = std::move(chunk);

    auto cb = GetCallbackCopy(HttpClientStage::kChunk);
    if (cb) {
      cb(result);
    }
  }

  void EmitDone(const HttpClientResult& result) {
    auto cb = GetDoneCallbackCopy();
    if (cb) {
      cb(result);
    }
  }

  void EmitStageByResult(const HttpClientResult& result) {
    auto cb = GetCallbackCopy(result.stage);
    if (cb) {
      cb(result);
    }
  }

  bool HasChunkCallback() const {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    return static_cast<bool>(on_chunk_);
  }

  Callback GetCallbackCopy(HttpClientStage stage) const {
    // Copy the callback under lock, then invoke outside the lock.
    std::lock_guard<std::mutex> lock(callback_mutex_);
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
    return {};
  }

  Callback GetDoneCallbackCopy() const {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    return on_done_;
  }

  static HttpClientStage ErrorStageToCallbackStage(
      HttpClientErrorStage error_stage) {
    switch (error_stage) {
      case HttpClientErrorStage::kReadHeader:
        return HttpClientStage::kHeader;
      case HttpClientErrorStage::kReadBody:
        return HttpClientStage::kChunk;
      case HttpClientErrorStage::kNone:
      case HttpClientErrorStage::kCreate:
      case HttpClientErrorStage::kResolve:
      case HttpClientErrorStage::kConnect:
      case HttpClientErrorStage::kTlsHandshake:
      case HttpClientErrorStage::kWriteRequest:
      default:
        return HttpClientStage::kConnected;
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
  HttpClientRequest request_;

  std::string host_;
  std::string port_;
  std::string target_;
  HttpClientOptions options_;

  bool use_ssl_{false};
  boost::asio::ssl::context* ssl_ctx_{nullptr};

  bool started_{false};
  bool done_{false};
  bool cancelled_{false};
  bool failed_{false};
  std::size_t last_emitted_body_size_{0};

  std::optional<boost::system::error_code> create_error_{};
  HttpClientErrorStage create_error_stage_{HttpClientErrorStage::kNone};

  boost::system::error_code error_code_{};
  HttpClientErrorStage error_stage_{HttpClientErrorStage::kNone};

  mutable std::mutex callback_mutex_;
  Callback on_connected_;
  Callback on_header_;
  Callback on_chunk_;
  Callback on_done_;
};

HttpClientTask::HttpClientTask(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

HttpClientTask::~HttpClientTask() = default;

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttp(
    boost::asio::any_io_executor executor, std::string host, std::string port,
    std::string target, http::verb method, HttpClientOptions options) {
  auto impl = std::make_shared<Impl>(std::move(executor), std::move(host),
                                     std::move(port), std::move(target), method,
                                     std::move(options), false, nullptr);
  return std::shared_ptr<HttpClientTask>(new HttpClientTask(std::move(impl)));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateHttps(
    boost::asio::any_io_executor executor, boost::asio::ssl::context& ssl_ctx,
    std::string host, std::string port, std::string target, http::verb method,
    HttpClientOptions options) {
  auto impl = std::make_shared<Impl>(std::move(executor), std::move(host),
                                     std::move(port), std::move(target), method,
                                     std::move(options), true, &ssl_ctx);
  return std::shared_ptr<HttpClientTask>(new HttpClientTask(std::move(impl)));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateFromUrl(
    boost::asio::any_io_executor executor, std::string url, http::verb method,
    HttpClientOptions options) {
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    auto impl = std::make_shared<Impl>(std::move(executor), "", "", "/", method,
                                       std::move(options), false, nullptr);
    impl->SetCreateError(make_error_code(boost::system::errc::invalid_argument),
                         HttpClientErrorStage::kCreate);
    return std::shared_ptr<HttpClientTask>(new HttpClientTask(std::move(impl)));
  }

  auto impl = std::make_shared<Impl>(
      std::move(executor), parsed->host, parsed->port, parsed->target, method,
      std::move(options), parsed->https, nullptr);

  if (parsed->https) {
    impl->SetCreateError(make_error_code(boost::system::errc::invalid_argument),
                         HttpClientErrorStage::kCreate);
  }

  return std::shared_ptr<HttpClientTask>(new HttpClientTask(std::move(impl)));
}

std::shared_ptr<HttpClientTask> HttpClientTask::CreateFromUrl(
    boost::asio::any_io_executor executor, boost::asio::ssl::context& ssl_ctx,
    std::string url, http::verb method, HttpClientOptions options) {
  auto parsed = ParseHttpUrl(url);
  if (!parsed) {
    auto impl = std::make_shared<Impl>(std::move(executor), "", "", "/", method,
                                       std::move(options), false, nullptr);
    impl->SetCreateError(make_error_code(boost::system::errc::invalid_argument),
                         HttpClientErrorStage::kCreate);
    return std::shared_ptr<HttpClientTask>(new HttpClientTask(std::move(impl)));
  }

  auto impl = std::make_shared<Impl>(
      std::move(executor), parsed->host, parsed->port, parsed->target, method,
      std::move(options), parsed->https, parsed->https ? &ssl_ctx : nullptr);
  return std::shared_ptr<HttpClientTask>(new HttpClientTask(std::move(impl)));
}

HttpClientTask& HttpClientTask::OnConnected(Callback cb) {
  impl_->SetOnConnected(std::move(cb));
  return *this;
}

HttpClientTask& HttpClientTask::OnHeader(Callback cb) {
  impl_->SetOnHeader(std::move(cb));
  return *this;
}

HttpClientTask& HttpClientTask::OnChunk(Callback cb) {
  impl_->SetOnChunk(std::move(cb));
  return *this;
}

HttpClientTask& HttpClientTask::OnDone(Callback cb) {
  impl_->SetOnDone(std::move(cb));
  return *this;
}

HttpClientRequest& HttpClientTask::Request() noexcept { return impl_->Request(); }

void HttpClientTask::Start() { impl_->Start(); }

void HttpClientTask::Cancel() { impl_->Cancel(); }

bool HttpClientTask::Failed() const noexcept { return impl_->Failed(); }

boost::system::error_code HttpClientTask::ErrorCode() const noexcept {
  return impl_->ErrorCode();
}

HttpClientErrorStage HttpClientTask::ErrorStage() const noexcept {
  return impl_->ErrorStage();
}

}  // namespace bsrvcore
