/**
 * @file websocket_client_task_stream.cc
 * @brief Runtime read/write/close flow for WebSocketClientTask.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-05
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <boost/asio/buffer.hpp>
#include <boost/beast/core/basic_stream.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/error.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/system/errc.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/client_stream.h"
#include "bsrvcore/connection/client/client_websocket_stream.h"
#include "bsrvcore/connection/client/websocket_client_task.h"
#include "bsrvcore/connection/websocket/websocket_task_base.h"
#include "bsrvcore/core/types.h"

namespace bsrvcore {

namespace {

namespace websocket = boost::beast::websocket;
using tcp = Tcp;

template <typename Stream, typename Handler>
void StartWebSocketCloseForStream(Stream& stream, Handler&& handler) {
  stream.async_close(websocket::close_code::normal,
                     std::forward<Handler>(handler));
}

template <typename Stream>
void AbortTransport(Stream& stream) {
  boost::system::error_code ignore_ec;
  auto& socket = boost::beast::get_lowest_layer(stream).socket();
  socket.shutdown(tcp::socket::shutdown_both, ignore_ec);
  socket.close(ignore_ec);
}

}  // namespace

void WebSocketClientTask::Cancel() {
  if (IsClosingOrClosed()) {
    return;
  }

  close_reason_ = CloseReason::kUserCancel;
  const bool was_open = IsOpen();
  lifecycle_state_ = LifecycleState::kClosing;

  if (was_open) {
    auto self = shared_from_this();
    if (websocket_stream_.IsWs()) {
      StartWebSocketCloseForStream(
          websocket_stream_.Ws(),
          [self](boost::system::error_code ec) { self->NotifyCloseOnce(ec); });
      return;
    }
    if (websocket_stream_.IsWss()) {
      StartWebSocketCloseForStream(
          websocket_stream_.Wss(),
          [self](boost::system::error_code ec) { self->NotifyCloseOnce(ec); });
      return;
    }
  } else {
    pending_transport_.Close();
    if (websocket_stream_.IsWs()) {
      AbortTransport(websocket_stream_.Ws());
    }
    if (websocket_stream_.IsWss()) {
      AbortTransport(websocket_stream_.Wss());
    }
  }

  NotifyCloseOnce(boost::system::error_code{});
}

bool WebSocketClientTask::WriteMessage(std::string payload, bool binary) {
  if (!IsOpen()) {
    return false;
  }

  auto payload_sp = AllocateShared<std::string>(std::move(payload));
  auto self = shared_from_this();
  if (websocket_stream_.IsWs()) {
    auto& stream = websocket_stream_.Ws();
    stream.binary(binary);
    stream.async_write(
        boost::asio::buffer(*payload_sp),
        [self, payload_sp](boost::system::error_code ec, std::size_t) {
          if (ec) {
            self->NotifyError(ec, "websocket client write failed");
          }
        });
    return true;
  }
  if (websocket_stream_.IsWss()) {
    auto& stream = websocket_stream_.Wss();
    stream.binary(binary);
    stream.async_write(
        boost::asio::buffer(*payload_sp),
        [self, payload_sp](boost::system::error_code ec, std::size_t) {
          if (ec) {
            self->NotifyError(ec, "websocket client write failed");
          }
        });
    return true;
  }

  return false;
}

bool WebSocketClientTask::WriteControl(WebSocketControlKind kind,
                                       std::string payload) {
  if (!IsOpen()) {
    return false;
  }

  switch (kind) {
    case WebSocketControlKind::kPing:
      return WritePingControl(std::move(payload));
    case WebSocketControlKind::kPong:
      return WritePongControl(std::move(payload));
    case WebSocketControlKind::kClose:
      return WriteCloseControl();
  }

  return false;
}

bool WebSocketClientTask::WritePingControl(std::string payload) {
  auto self = shared_from_this();
  if (websocket_stream_.IsWs()) {
    websocket_stream_.Ws().async_ping(
        websocket::ping_data(std::move(payload)),
        [self](boost::system::error_code ec) {
          if (ec) {
            self->NotifyError(ec, "websocket client ping failed");
          }
        });
    return true;
  }
  if (websocket_stream_.IsWss()) {
    websocket_stream_.Wss().async_ping(
        websocket::ping_data(std::move(payload)),
        [self](boost::system::error_code ec) {
          if (ec) {
            self->NotifyError(ec, "websocket client ping failed");
          }
        });
    return true;
  }

  return false;
}

bool WebSocketClientTask::WritePongControl(std::string payload) {
  auto self = shared_from_this();
  if (websocket_stream_.IsWs()) {
    websocket_stream_.Ws().async_pong(
        websocket::ping_data(std::move(payload)),
        [self](boost::system::error_code ec) {
          if (ec) {
            self->NotifyError(ec, "websocket client pong failed");
          }
        });
    return true;
  }
  if (websocket_stream_.IsWss()) {
    websocket_stream_.Wss().async_pong(
        websocket::ping_data(std::move(payload)),
        [self](boost::system::error_code ec) {
          if (ec) {
            self->NotifyError(ec, "websocket client pong failed");
          }
        });
    return true;
  }

  return false;
}

bool WebSocketClientTask::WriteCloseControl() {
  Cancel();
  return true;
}

void WebSocketClientTask::NotifyCloseOnce(boost::system::error_code ec) {
  if (lifecycle_state_ == LifecycleState::kClosed) {
    return;
  }

  lifecycle_state_ = LifecycleState::kClosed;
  NotifyClose(ec);
}

void WebSocketClientTask::FailAndClose(boost::system::error_code ec,
                                       std::string message) {
  if (close_reason_ == CloseReason::kNone) {
    close_reason_ = CloseReason::kError;
  }
  if (lifecycle_state_ != LifecycleState::kClosed) {
    lifecycle_state_ = LifecycleState::kClosing;
  }
  if (ec) {
    NotifyError(ec, std::move(message));
  }
  NotifyCloseOnce(ec);
}

void WebSocketClientTask::BeginReadLoop() {
  // Call chain: OnHandshakeComplete → BeginReadLoop
  //   → websocket_stream_.async_read → OnWebSocketRead
  //
  // Both ws and wss branches share the same completion handler.
  if (!IsOpen()) {
    return;
  }

  auto self = shared_from_this();
  if (websocket_stream_.IsWs()) {
    WebSocketStream* stream = &websocket_stream_.Ws();
    stream->async_read(
        ws_read_buffer_,
        [self, stream](boost::system::error_code ec, std::size_t) {
          std::string payload =
              boost::beast::buffers_to_string(self->ws_read_buffer_.data());
          self->ws_read_buffer_.consume(self->ws_read_buffer_.size());
          self->OnWebSocketRead(ec, stream->got_binary(), std::move(payload));
        });
    return;
  }
  if (websocket_stream_.IsWss()) {
    SecureWebSocketStream* stream = &websocket_stream_.Wss();
    stream->async_read(
        ws_read_buffer_,
        [self, stream](boost::system::error_code ec, std::size_t) {
          std::string payload =
              boost::beast::buffers_to_string(self->ws_read_buffer_.data());
          self->ws_read_buffer_.consume(self->ws_read_buffer_.size());
          self->OnWebSocketRead(ec, stream->got_binary(), std::move(payload));
        });
  }
}

void WebSocketClientTask::OnWebSocketRead(boost::system::error_code ec,
                                          bool binary, std::string payload) {
  // Handle close frame or error.
  if (IsClosingOrClosed()) {
    NotifyCloseOnce(boost::system::error_code{});
    return;
  }
  if (ec) {
    if (ec == websocket::error::closed) {
      close_reason_ = CloseReason::kRemoteClose;
      lifecycle_state_ = LifecycleState::kClosing;
      NotifyCloseOnce(boost::system::error_code{});
      return;
    }
    FailAndClose(ec, "websocket client read failed");
    return;
  }

  // Dispatch received message and continue reading.
  WebSocketMessage message;
  message.binary = binary;
  message.payload = std::move(payload);
  NotifyReadMessage(std::move(message));
  BeginReadLoop();
}

}  // namespace bsrvcore
