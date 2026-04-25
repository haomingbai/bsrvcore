/**
 * @file websocket_client_task_startup.cc
 * @brief Startup and handshake flow for WebSocketClientTask.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-05
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/system/errc.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/http_client_session.h"
#include "bsrvcore/connection/client/websocket_client_task.h"

namespace bsrvcore {

namespace {

namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
using tcp = Tcp;

bool IsWebSocketReservedHeader(http::field field) {
  switch (field) {
    case http::field::connection:
    case http::field::upgrade:
    case http::field::sec_websocket_accept:
    case http::field::sec_websocket_extensions:
    case http::field::sec_websocket_key:
    case http::field::sec_websocket_protocol:
    case http::field::sec_websocket_version:
      return true;
    default:
      return false;
  }
}

template <typename Stream>
void ConfigureClientHandshake(Stream& stream, const HttpClientRequest& request,
                              const HttpClientOptions& options) {
  auto timeout = websocket::stream_base::timeout::suggested(
      boost::beast::role_type::client);
  timeout.handshake_timeout =
      std::max(options.write_timeout, options.read_header_timeout);
  stream.set_option(timeout);
  stream.set_option(websocket::stream_base::decorator(
      [request](websocket::request_type& ws_req) {
        for (const auto& field : request) {
          if (field.name() == http::field::unknown ||
              IsWebSocketReservedHeader(field.name())) {
            continue;
          }
          ws_req.set(field.name_string(), field.value());
        }
      }));
  boost::beast::get_lowest_layer(stream).expires_never();
}

template <typename Stream, typename Handler>
void StartTcpConnectForStream(Stream& stream,
                              const tcp::resolver::results_type& results,
                              std::chrono::milliseconds timeout,
                              Handler&& handler) {
  auto& lowest = boost::beast::get_lowest_layer(stream);
  lowest.expires_after(timeout);
  lowest.async_connect(results, std::forward<Handler>(handler));
}

template <typename Stream, typename Handler>
void StartWebSocketHandshakeForStream(
    Stream& stream, std::shared_ptr<websocket::response_type> response,
    const std::string& host, const std::string& target, Handler&& handler) {
  stream.async_handshake(*response, host, target,
                         std::forward<Handler>(handler));
}

}  // namespace

void WebSocketClientTask::Start() {
  if (lifecycle_state_ != LifecycleState::kInit) {
    return;
  }
  lifecycle_state_ = LifecycleState::kStarting;

  if (create_error_) {
    FailAndClose(*create_error_, create_error_message_.empty()
                                     ? "websocket client create failed"
                                     : create_error_message_);
    return;
  }

  if (!PrepareStart()) {
    return;
  }
  if (startup_mode_ != StartupMode::kDial) {
    if (!CreateTransportFromRaw()) {
      return;
    }
    StartWebSocketHandshake();
    return;
  }

  if (!CreateTransport()) {
    return;
  }

  StartResolve();
}

bool WebSocketClientTask::PrepareStart() {
  if (auto session = session_.lock()) {
    session->MaybeInjectCookies(request_, host_, target_, use_ssl_);
  }

  if (use_ssl_ && startup_mode_ == StartupMode::kDial && ssl_ctx_ == nullptr) {
    FailAndClose(make_error_code(boost::system::errc::invalid_argument),
                 "wss requires an SSL context");
    return false;
  }

  if (request_.find(http::field::host) == request_.end()) {
    request_.set(http::field::host, host_);
  }
  if (request_.find(http::field::user_agent) == request_.end() &&
      !options_.user_agent.empty()) {
    request_.set(http::field::user_agent, options_.user_agent);
  }
  request_.target(target_);
  request_.method(http::verb::get);

  return true;
}

bool WebSocketClientTask::CreateTransport() {
  resolver_ = std::make_unique<tcp::resolver>(io_executor_);
  ws_stream_.reset();
  wss_stream_.reset();

  if (use_ssl_) {
    wss_stream_ =
        std::make_unique<SecureWebSocketStream>(io_executor_, *ssl_ctx_);
    return true;
  }

  ws_stream_ = std::make_unique<WebSocketStream>(io_executor_);
  return true;
}

bool WebSocketClientTask::CreateTransportFromRaw() {
  resolver_.reset();
  ws_stream_.reset();
  wss_stream_.reset();

  if (startup_mode_ == StartupMode::kRawTls) {
    if (raw_ssl_stream_ == nullptr) {
      FailAndClose(make_error_code(boost::system::errc::invalid_argument),
                   "wss raw factory requires a ready SSL stream");
      return false;
    }
    wss_stream_ =
        std::make_unique<SecureWebSocketStream>(std::move(*raw_ssl_stream_));
    raw_ssl_stream_.reset();
    return true;
  }

  if (startup_mode_ == StartupMode::kRawTcp) {
    if (raw_tcp_stream_ == nullptr) {
      FailAndClose(make_error_code(boost::system::errc::invalid_argument),
                   "ws raw factory requires a ready TCP stream");
      return false;
    }
    ws_stream_ = std::make_unique<WebSocketStream>(std::move(*raw_tcp_stream_));
    raw_tcp_stream_.reset();
    return true;
  }

  FailAndClose(make_error_code(boost::system::errc::invalid_argument),
               "raw startup mode is not configured");
  return false;
}

void WebSocketClientTask::StartResolve() {
  lifecycle_state_ = LifecycleState::kResolving;
  auto self = shared_from_this();
  resolver_->async_resolve(host_, port_,
                           [self](boost::system::error_code ec,
                                  tcp::resolver::results_type results) {
                             self->OnResolveCompleted(ec, std::move(results));
                           });
}

void WebSocketClientTask::OnResolveCompleted(
    boost::system::error_code ec, tcp::resolver::results_type results) {
  if (IsClosingOrClosed()) {
    NotifyCloseOnce(boost::system::error_code{});
    return;
  }
  if (ec) {
    FailAndClose(ec, "resolve failed");
    return;
  }

  StartTcpConnect(results);
}

void WebSocketClientTask::StartTcpConnect(
    const tcp::resolver::results_type& results) {
  lifecycle_state_ = LifecycleState::kConnecting;
  auto self = shared_from_this();
  if (ws_stream_ != nullptr) {
    StartTcpConnectForStream(
        *ws_stream_, results, options_.connect_timeout,
        [self](boost::system::error_code ec, const tcp::endpoint&) {
          self->OnTcpConnectCompleted(ec);
        });
    return;
  }
  if (wss_stream_ != nullptr) {
    StartTcpConnectForStream(
        *wss_stream_, results, options_.connect_timeout,
        [self](boost::system::error_code ec, const tcp::endpoint&) {
          self->OnTcpConnectCompleted(ec);
        });
    return;
  }

  FailAndClose(make_error_code(boost::system::errc::not_connected),
               "connect failed");
}

void WebSocketClientTask::OnTcpConnectCompleted(boost::system::error_code ec) {
  if (IsClosingOrClosed()) {
    NotifyCloseOnce(boost::system::error_code{});
    return;
  }
  if (ec) {
    FailAndClose(ec, "connect failed");
    return;
  }

  if (use_ssl_) {
    StartTlsHandshake();
    return;
  }

  StartWebSocketHandshake();
}

void WebSocketClientTask::StartTlsHandshake() {
  if (wss_stream_ == nullptr) {
    FailAndClose(make_error_code(boost::system::errc::invalid_argument),
                 "tls handshake failed");
    return;
  }

  lifecycle_state_ = LifecycleState::kTlsHandshaking;

  auto& tls_stream = wss_stream_->next_layer();
  if (SSL_set_tlsext_host_name(tls_stream.native_handle(), host_.c_str()) !=
      1) {
    boost::system::error_code const sni_ec{
        static_cast<int>(::ERR_get_error()),
        boost::asio::error::get_ssl_category()};
    FailAndClose(sni_ec, "tls handshake failed");
    return;
  }

  if (options_.verify_peer) {
    tls_stream.set_verify_mode(boost::asio::ssl::verify_peer);
    tls_stream.set_verify_callback(
        boost::asio::ssl::host_name_verification(host_));
  } else {
    tls_stream.set_verify_mode(boost::asio::ssl::verify_none);
  }

  boost::beast::get_lowest_layer(*wss_stream_)
      .expires_after(options_.tls_handshake_timeout);
  auto self = shared_from_this();
  tls_stream.async_handshake(boost::asio::ssl::stream_base::client,
                             [self](boost::system::error_code ec) {
                               self->OnTlsHandshakeCompleted(ec);
                             });
}

void WebSocketClientTask::OnTlsHandshakeCompleted(
    boost::system::error_code ec) {
  if (IsClosingOrClosed()) {
    NotifyCloseOnce(boost::system::error_code{});
    return;
  }
  if (ec) {
    FailAndClose(ec, "tls handshake failed");
    return;
  }

  StartWebSocketHandshake();
}

void WebSocketClientTask::StartWebSocketHandshake() {
  lifecycle_state_ = LifecycleState::kWsHandshaking;
  auto response_sp = AllocateShared<websocket::response_type>();
  auto self = shared_from_this();

  if (ws_stream_ != nullptr) {
    ConfigureClientHandshake(*ws_stream_, request_, options_);
    StartWebSocketHandshakeForStream(
        *ws_stream_, response_sp, host_, target_,
        [self, response_sp](boost::system::error_code ec) {
          self->OnWebSocketHandshakeCompleted(ec, response_sp);
        });
    return;
  }
  if (wss_stream_ != nullptr) {
    ConfigureClientHandshake(*wss_stream_, request_, options_);
    StartWebSocketHandshakeForStream(
        *wss_stream_, response_sp, host_, target_,
        [self, response_sp](boost::system::error_code ec) {
          self->OnWebSocketHandshakeCompleted(ec, response_sp);
        });
    return;
  }

  FailAndClose(make_error_code(boost::system::errc::not_connected),
               "websocket handshake failed");
}

void WebSocketClientTask::OnWebSocketHandshakeCompleted(
    boost::system::error_code ec,
    std::shared_ptr<websocket::response_type> response_sp) {
  if (IsClosingOrClosed()) {
    NotifyCloseOnce(boost::system::error_code{});
    return;
  }

  HttpClientResponse response;
  response.result(response_sp->result());
  response.version(response_sp->version());
  response.reason(response_sp->reason());
  for (const auto& field : *response_sp) {
    response.set(field.name_string(), field.value());
  }
  HttpResponseHeader header = response.base();

  SyncHandshakeSetCookies(*response_sp);
  EmitHttpDone(ec, header, response);

  if (ec) {
    FailAndClose(ec, "websocket handshake failed");
    return;
  }

  lifecycle_state_ = LifecycleState::kOpen;
  NotifyOpen();
  BeginReadLoop();
}

void WebSocketClientTask::SyncHandshakeSetCookies(
    const websocket::response_type& response) {
  auto session = session_.lock();
  if (!session) {
    return;
  }

  for (const auto& field : response) {
    if (field.name() == http::field::set_cookie) {
      session->SyncSetCookie(host_, target_, field.value());
    }
  }
}

}  // namespace bsrvcore
