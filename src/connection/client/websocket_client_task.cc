/**
 * @file websocket_client_task.cc
 * @brief WebSocket client task scaffolding.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-05
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/client/websocket_client_task.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/system/errc.hpp>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/http_client_session.h"

namespace bsrvcore {

namespace {

void ConfigureUpgradeHeaders(HttpClientRequest& request) {
  request.method(boost::beast::http::verb::get);
  request.set(boost::beast::http::field::connection, "Upgrade");
  request.set(boost::beast::http::field::upgrade, "websocket");
  request.set(boost::beast::http::field::sec_websocket_version, "13");
  // Placeholder key used for the first implementation stage.
  request.set(boost::beast::http::field::sec_websocket_key,
              "dGhlIHNhbXBsZSBub25jZQ==");
}

bool ParseWebSocketUrl(const std::string& url, bool* out_ssl, std::string* host,
                       std::string* port, std::string* target) {
  constexpr const char* kWsPrefix = "ws://";
  constexpr const char* kWssPrefix = "wss://";
  std::size_t offset = 0;

  if (url.rfind(kWsPrefix, 0) == 0) {
    *out_ssl = false;
    offset = 5;
  } else if (url.rfind(kWssPrefix, 0) == 0) {
    *out_ssl = true;
    offset = 6;
  } else {
    return false;
  }

  const auto slash = url.find('/', offset);
  const auto host_port = slash == std::string::npos
                             ? url.substr(offset)
                             : url.substr(offset, slash - offset);
  *target = slash == std::string::npos ? "/" : url.substr(slash);

  const auto colon = host_port.rfind(':');
  if (colon == std::string::npos) {
    *host = host_port;
    *port = *out_ssl ? "443" : "80";
  } else {
    *host = host_port.substr(0, colon);
    *port = host_port.substr(colon + 1);
  }

  return !host->empty() && !port->empty() && !target->empty();
}

bool IsWebSocketReservedHeader(boost::beast::http::field field) {
  switch (field) {
    case boost::beast::http::field::connection:
    case boost::beast::http::field::upgrade:
    case boost::beast::http::field::sec_websocket_accept:
    case boost::beast::http::field::sec_websocket_extensions:
    case boost::beast::http::field::sec_websocket_key:
    case boost::beast::http::field::sec_websocket_protocol:
    case boost::beast::http::field::sec_websocket_version:
      return true;
    default:
      return false;
  }
}

}  // namespace

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::CreateHttp(
    Executor io_executor, std::string host, std::string port,
    std::string target, HandlerPtr handler, HttpClientOptions options) {
  (void)options;
  auto ws_task = AllocateShared<WebSocketClientTask>(
      std::move(io_executor), std::move(host), std::move(port),
      std::move(target), std::move(handler), false, nullptr);
  ConfigureUpgradeHeaders(ws_task->Request());
  return ws_task;
}

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::CreateHttps(
    Executor io_executor, boost::asio::ssl::context& ssl_ctx, std::string host,
    std::string port, std::string target, HandlerPtr handler,
    HttpClientOptions options) {
  (void)options;
  auto ws_task = AllocateShared<WebSocketClientTask>(
      std::move(io_executor), std::move(host), std::move(port),
      std::move(target), std::move(handler), true, &ssl_ctx);
  ConfigureUpgradeHeaders(ws_task->Request());
  return ws_task;
}

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::CreateFromUrl(
    Executor io_executor, std::string url, HandlerPtr handler,
    HttpClientOptions options) {
  (void)options;
  bool use_ssl = false;
  std::string host;
  std::string port;
  std::string target;
  if (!ParseWebSocketUrl(url, &use_ssl, &host, &port, &target)) {
    return nullptr;
  }

  auto ws_task = AllocateShared<WebSocketClientTask>(
      std::move(io_executor), std::move(host), std::move(port),
      std::move(target), std::move(handler), use_ssl, nullptr);
  ConfigureUpgradeHeaders(ws_task->Request());
  return ws_task;
}

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::CreateFromUrl(
    Executor io_executor, boost::asio::ssl::context& ssl_ctx, std::string url,
    HandlerPtr handler, HttpClientOptions options) {
  (void)options;
  bool use_ssl = false;
  std::string host;
  std::string port;
  std::string target;
  if (!ParseWebSocketUrl(url, &use_ssl, &host, &port, &target)) {
    return nullptr;
  }

  auto ws_task = AllocateShared<WebSocketClientTask>(
      std::move(io_executor), std::move(host), std::move(port),
      std::move(target), std::move(handler), use_ssl, &ssl_ctx);
  ConfigureUpgradeHeaders(ws_task->Request());
  return ws_task;
}

std::shared_ptr<WebSocketClientTask> WebSocketClientTask::OnHttpDone(
    HttpDoneCallback cb) {
  on_http_done_ = std::move(cb);
  return shared_from_this();
}

HttpClientRequest& WebSocketClientTask::Request() noexcept { return request_; }

void WebSocketClientTask::SetJson(const JsonValue& value) {
  request_.body() = json::serialize(value);
  request_.set(boost::beast::http::field::content_type, "application/json");
}

void WebSocketClientTask::SetJson(JsonValue&& value) {
  SetJson(static_cast<const JsonValue&>(value));
}

void WebSocketClientTask::AttachSession(
    std::weak_ptr<HttpClientSession> session) {
  session_ = std::move(session);
}

void WebSocketClientTask::Start() {
  if (started_ || cancelled_) {
    return;
  }

  if (!PrecheckAndPrepareStart()) {
    return;
  }

  StartResolve();
}

bool WebSocketClientTask::PrecheckAndPrepareStart() {
  if (auto session = session_.lock()) {
    session->MaybeInjectCookies(request_, host_, target_, use_ssl_);
  }

  if (use_ssl_) {
    FailAndClose(make_error_code(boost::system::errc::operation_not_supported),
                 "wss transport is not wired yet");
    return false;
  }

  resolver_ = std::make_unique<boost::asio::ip::tcp::resolver>(io_executor_);
  ws_stream_.emplace(io_executor_);

  return true;
}

void WebSocketClientTask::StartResolve() {
  auto self = shared_from_this();
  resolver_->async_resolve(
      host_, port_,
      [self](boost::system::error_code ec,
             boost::asio::ip::tcp::resolver::results_type results) {
        self->OnResolveCompleted(ec, std::move(results));
      });
}

void WebSocketClientTask::OnResolveCompleted(
    boost::system::error_code ec,
    boost::asio::ip::tcp::resolver::results_type results) {
  if (ec || !ws_stream_.has_value()) {
    FailAndClose(ec ? ec : make_error_code(boost::system::errc::not_connected),
                 "resolve failed");
    return;
  }

  auto self = shared_from_this();
  boost::asio::async_connect(ws_stream_->next_layer(), results,
                             [self](boost::system::error_code connect_ec,
                                    const boost::asio::ip::tcp::endpoint&) {
                               self->OnConnectCompleted(connect_ec);
                             });
}

void WebSocketClientTask::OnConnectCompleted(boost::system::error_code ec) {
  if (ec || !ws_stream_.has_value()) {
    FailAndClose(ec ? ec : make_error_code(boost::system::errc::not_connected),
                 "connect failed");
    return;
  }

  StartHandshake();
}

void WebSocketClientTask::StartHandshake() {
  ws_stream_->set_option(boost::beast::websocket::stream_base::decorator(
      [request = request_](boost::beast::websocket::request_type& ws_req) {
        for (const auto& field : request) {
          if (field.name() == boost::beast::http::field::unknown ||
              IsWebSocketReservedHeader(field.name())) {
            continue;
          }
          ws_req.set(field.name_string(), field.value());
        }
      }));

  auto response_sp = AllocateShared<boost::beast::websocket::response_type>();
  auto self = shared_from_this();
  ws_stream_->async_handshake(
      *response_sp, host_, target_,
      [self, response_sp](boost::system::error_code hs_ec) {
        self->OnHandshakeCompleted(hs_ec, response_sp);
      });
}

void WebSocketClientTask::OnHandshakeCompleted(
    boost::system::error_code ec,
    std::shared_ptr<boost::beast::websocket::response_type> response_sp) {
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

  started_ = true;
  NotifyOpen();
  BeginReadLoop();
}

void WebSocketClientTask::SyncHandshakeSetCookies(
    const boost::beast::websocket::response_type& response) {
  auto session = session_.lock();
  if (!session) {
    return;
  }

  for (const auto& field : response) {
    if (field.name() == boost::beast::http::field::set_cookie) {
      session->SyncSetCookie(host_, target_, field.value());
    }
  }
}

void WebSocketClientTask::Cancel() {
  if (cancelled_) {
    return;
  }

  cancelled_ = true;
  if (ws_stream_.has_value()) {
    auto self = shared_from_this();
    ws_stream_->async_close(
        boost::beast::websocket::close_code::normal,
        [self](boost::system::error_code ec) { self->NotifyCloseOnce(ec); });
    return;
  }

  NotifyCloseOnce(boost::system::error_code{});
}

bool WebSocketClientTask::WriteMessage(std::string payload, bool binary) {
  if (!started_ || cancelled_ || !ws_stream_.has_value()) {
    return false;
  }

  auto payload_sp = AllocateShared<std::string>(std::move(payload));
  ws_stream_->binary(binary);
  auto self = shared_from_this();
  ws_stream_->async_write(
      boost::asio::buffer(*payload_sp),
      [self, payload_sp](boost::system::error_code ec, std::size_t) {
        if (ec) {
          self->NotifyError(ec, "websocket client write failed");
        }
      });
  return true;
}

bool WebSocketClientTask::WriteControl(WebSocketControlKind kind,
                                       std::string payload) {
  if (!started_ || cancelled_ || !ws_stream_.has_value()) {
    return false;
  }

  auto self = shared_from_this();
  switch (kind) {
    case WebSocketControlKind::kPing:
      ws_stream_->async_ping(boost::beast::websocket::ping_data(payload),
                             [self](boost::system::error_code ec) {
                               if (ec) {
                                 self->NotifyError(
                                     ec, "websocket client ping failed");
                               }
                             });
      return true;
    case WebSocketControlKind::kPong:
      ws_stream_->async_pong(boost::beast::websocket::ping_data(payload),
                             [self](boost::system::error_code ec) {
                               if (ec) {
                                 self->NotifyError(
                                     ec, "websocket client pong failed");
                               }
                             });
      return true;
    case WebSocketControlKind::kClose:
      Cancel();
      return true;
  }

  return false;
}

WebSocketClientTask::~WebSocketClientTask() = default;

WebSocketClientTask::WebSocketClientTask(Executor io_executor, std::string host,
                                         std::string port, std::string target,
                                         HandlerPtr handler, bool use_ssl,
                                         boost::asio::ssl::context* ssl_ctx)
    : WebSocketTaskBase(std::move(handler)),
      io_executor_(std::move(io_executor)),
      host_(std::move(host)),
      port_(std::move(port)),
      target_(std::move(target)),
      use_ssl_(use_ssl),
      ssl_ctx_(ssl_ctx) {
  request_.method(boost::beast::http::verb::get);
  request_.target(target_);
  request_.set(boost::beast::http::field::host, host_);
}

void WebSocketClientTask::EmitHttpDone(boost::system::error_code ec,
                                       HttpResponseHeader header,
                                       HttpClientResponse response) {
  HttpClientResult result;
  result.ec = ec;
  result.stage = HttpClientStage::kDone;
  result.header = std::move(header);
  result.response = std::move(response);

  if (on_http_done_) {
    on_http_done_(result);
  }
}

void WebSocketClientTask::NotifyCloseOnce(boost::system::error_code ec) {
  if (close_notified_) {
    return;
  }

  close_notified_ = true;
  NotifyClose(ec);
}

void WebSocketClientTask::FailAndClose(boost::system::error_code ec,
                                       std::string message) {
  if (ec) {
    NotifyError(ec, std::move(message));
  }
  NotifyCloseOnce(ec);
}

void WebSocketClientTask::BeginReadLoop() {
  if (!started_ || cancelled_ || !ws_stream_.has_value()) {
    return;
  }

  auto self = shared_from_this();
  ws_stream_->async_read(
      ws_read_buffer_, [self](boost::system::error_code ec, std::size_t) {
        if (ec) {
          if (ec == boost::beast::websocket::error::closed) {
            self->cancelled_ = true;
            self->NotifyCloseOnce(boost::system::error_code{});
            return;
          }
          self->FailAndClose(ec, "websocket client read failed");
          return;
        }

        WebSocketMessage message;
        message.binary = self->ws_stream_->got_binary();
        message.payload =
            boost::beast::buffers_to_string(self->ws_read_buffer_.data());
        self->ws_read_buffer_.consume(self->ws_read_buffer_.size());
        self->NotifyReadMessage(std::move(message));
        self->BeginReadLoop();
      });
}

}  // namespace bsrvcore
