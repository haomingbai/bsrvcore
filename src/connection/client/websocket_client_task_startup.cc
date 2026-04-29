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
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/stream_builder.h"
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

  DoStart();
}

void WebSocketClientTask::DoStart() {
  // Raw mode: WebSocket stream already created via
  // CreateHttpRaw/CreateHttpsRaw.
  if (!assembler_) {
    if (!ws_stream_ && !wss_stream_) {
      FailAndClose(make_error_code(boost::system::errc::invalid_argument),
                   "raw mode requires a pre-connected stream");
      return;
    }
    StartWebSocketHandshake();
    return;
  }

  DoAcquire();
}

void WebSocketClientTask::DoAcquire() {
  // Call chain: DoStart → DoAcquire → assembler_.Assemble
  //   → builder_.Acquire → OnAcquireComplete
  //
  // Assembler prepares the request (headers, cookies).
  // Builder resolves DNS, connects TCP, and optionally handshakes TLS.
  lifecycle_state_ = LifecycleState::kAcquiring;

  auto result = assembler_->Assemble(
      request_, options_, use_ssl_ ? "https" : "http", host_, port_, ssl_ctx_);
  request_ = std::move(result.request);
  connection_key_ = result.connection_key;

  auto self = shared_from_this();
  builder_->Acquire(connection_key_, io_executor_,
                    [self](boost::system::error_code ec, StreamSlot slot) {
                      self->OnAcquireComplete(ec, std::move(slot));
                    });
}

void WebSocketClientTask::OnAcquireComplete(boost::system::error_code ec,
                                            StreamSlot slot) {
  if (IsClosingOrClosed()) {
    NotifyCloseOnce(boost::system::error_code{});
    return;
  }

  if (ec) {
    // Classify the error based on the error code.
    const bool is_ssl_error =
        ec.category() == boost::asio::error::get_ssl_category();
    const std::string message =
        is_ssl_error ? "tls handshake failed" : "connection acquire failed";
    FailAndClose(ec, message);
    return;
  }

  // Create WebSocket stream on top of the acquired transport.
  if (slot.ssl_stream) {
    // Pre-handshaked SSL stream (e.g. from Raw mode).
    CreateSecureWebSocketStream(std::move(*slot.ssl_stream));
    StartWebSocketHandshake();
  } else if (slot.tcp_stream && slot.deferred_ssl_ctx) {
    // Deferred TLS handshake path for WSS.
    // WebSocketStreamBuilder returned a TcpStream; we now wrap it in
    // SslStream, perform TLS handshake, then create the WebSocket wrapper.
    DoDeferredTlsHandshake(std::move(*slot.tcp_stream),
                           std::move(slot.deferred_ssl_ctx),
                           slot.deferred_verify_peer);
  } else if (slot.tcp_stream) {
    // Plain WebSocket (WS).
    CreateWebSocketStream(std::move(*slot.tcp_stream));
    StartWebSocketHandshake();
  } else {
    FailAndClose(make_error_code(boost::system::errc::not_connected),
                 "acquire returned empty slot");
  }
}

void WebSocketClientTask::CreateWebSocketStream(TcpStream stream) {
  ws_stream_ = std::make_unique<WebSocketStream>(std::move(stream));
}

void WebSocketClientTask::CreateSecureWebSocketStream(SslStream stream) {
  wss_stream_ = std::make_unique<SecureWebSocketStream>(std::move(stream));
}

void WebSocketClientTask::DoDeferredTlsHandshake(TcpStream tcp_stream,
                                                 SslContextPtr ssl_ctx,
                                                 bool verify_peer) {
  // Call chain: OnAcquireComplete → DoDeferredTlsHandshake
  //   → ssl_stream.async_handshake → OnDeferredTlsHandshakeComplete
  //   → CreateSecureWebSocketStream → StartWebSocketHandshake
  //
  // Beast's websocket::stream<SslStream> expects TLS handshake to happen
  // on the next_layer() AFTER the WebSocket wrapper is constructed.
  // We create the SslStream from the connected TcpStream, set SNI and
  // verify mode, then perform the handshake.
  lifecycle_state_ = LifecycleState::kTlsHandshaking;

  auto ssl_stream = SslStream(std::move(tcp_stream), *ssl_ctx);

  // SNI hostname.
  if (SSL_set_tlsext_host_name(ssl_stream.native_handle(), host_.c_str()) !=
      1) {
    FailAndClose(
        boost::system::error_code{static_cast<int>(::ERR_get_error()),
                                  boost::asio::error::get_ssl_category()},
        "tls handshake failed");
    return;
  }

  // Peer verification.
  if (verify_peer) {
    ssl_stream.set_verify_mode(boost::asio::ssl::verify_peer);
    ssl_stream.set_verify_callback(
        boost::asio::ssl::host_name_verification(host_));
  } else {
    ssl_stream.set_verify_mode(boost::asio::ssl::verify_none);
  }

  boost::beast::get_lowest_layer(ssl_stream)
      .expires_after(options_.tls_handshake_timeout);

  // Wrap in shared_ptr so it outlives the handshake lambda.
  auto ssl_ptr = std::make_shared<SslStream>(std::move(ssl_stream));
  auto self = shared_from_this();
  (*ssl_ptr).async_handshake(boost::asio::ssl::stream_base::client,
                             [self, ssl_ptr](boost::system::error_code ec) {
                               self->OnDeferredTlsHandshakeComplete(
                                   ec, std::move(*ssl_ptr));
                             });
}

void WebSocketClientTask::OnDeferredTlsHandshakeComplete(
    boost::system::error_code ec, SslStream ssl_stream) {
  if (IsClosingOrClosed()) {
    NotifyCloseOnce(boost::system::error_code{});
    return;
  }

  if (ec) {
    FailAndClose(ec, "tls handshake failed");
    return;
  }

  // TLS handshake succeeded — create WebSocket wrapper and start WS handshake.
  CreateSecureWebSocketStream(std::move(ssl_stream));
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

  // Sync cookies from handshake response via assembler (if assembled mode).
  if (assembler_) {
    assembler_->OnResponseHeader(header, host_, target_);
  }

  EmitHttpDone(ec, header, response);

  if (ec) {
    FailAndClose(ec, "websocket handshake failed");
    return;
  }

  lifecycle_state_ = LifecycleState::kOpen;
  NotifyOpen();
  BeginReadLoop();
}

}  // namespace bsrvcore
