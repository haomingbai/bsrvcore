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
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/websocket_client_task.h"

namespace bsrvcore {

namespace {

namespace websocket = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;

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
  if (cancelled_) {
    return;
  }

  cancelled_ = true;
  if (resolver_) {
    resolver_->cancel();
  }

  if (opened_) {
    auto self = shared_from_this();
    if (ws_stream_ != nullptr) {
      StartWebSocketCloseForStream(
          *ws_stream_,
          [self](boost::system::error_code ec) { self->NotifyCloseOnce(ec); });
      return;
    }
    if (wss_stream_ != nullptr) {
      StartWebSocketCloseForStream(
          *wss_stream_,
          [self](boost::system::error_code ec) { self->NotifyCloseOnce(ec); });
      return;
    }
  } else {
    if (ws_stream_ != nullptr) {
      AbortTransport(*ws_stream_);
    }
    if (wss_stream_ != nullptr) {
      AbortTransport(*wss_stream_);
    }
  }

  NotifyCloseOnce(boost::system::error_code{});
}

bool WebSocketClientTask::WriteMessage(std::string payload, bool binary) {
  if (!opened_ || cancelled_) {
    return false;
  }

  auto payload_sp = AllocateShared<std::string>(std::move(payload));
  auto self = shared_from_this();
  if (ws_stream_ != nullptr) {
    ws_stream_->binary(binary);
    ws_stream_->async_write(
        boost::asio::buffer(*payload_sp),
        [self, payload_sp](boost::system::error_code ec, std::size_t) {
          if (ec) {
            self->NotifyError(ec, "websocket client write failed");
          }
        });
    return true;
  }
  if (wss_stream_ != nullptr) {
    wss_stream_->binary(binary);
    wss_stream_->async_write(
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
  if (!opened_ || cancelled_) {
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
  if (ws_stream_ != nullptr) {
    ws_stream_->async_ping(websocket::ping_data(std::move(payload)),
                           [self](boost::system::error_code ec) {
                             if (ec) {
                               self->NotifyError(
                                   ec, "websocket client ping failed");
                             }
                           });
    return true;
  }
  if (wss_stream_ != nullptr) {
    wss_stream_->async_ping(websocket::ping_data(std::move(payload)),
                            [self](boost::system::error_code ec) {
                              if (ec) {
                                self->NotifyError(
                                    ec, "websocket client ping failed");
                              }
                            });
    return true;
  }

  return false;
}

bool WebSocketClientTask::WritePongControl(std::string payload) {
  auto self = shared_from_this();
  if (ws_stream_ != nullptr) {
    ws_stream_->async_pong(websocket::ping_data(std::move(payload)),
                           [self](boost::system::error_code ec) {
                             if (ec) {
                               self->NotifyError(
                                   ec, "websocket client pong failed");
                             }
                           });
    return true;
  }
  if (wss_stream_ != nullptr) {
    wss_stream_->async_pong(websocket::ping_data(std::move(payload)),
                            [self](boost::system::error_code ec) {
                              if (ec) {
                                self->NotifyError(
                                    ec, "websocket client pong failed");
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
  if (close_notified_) {
    return;
  }

  close_notified_ = true;
  opened_ = false;
  NotifyClose(ec);
}

void WebSocketClientTask::FailAndClose(boost::system::error_code ec,
                                       std::string message) {
  opened_ = false;
  if (ec) {
    NotifyError(ec, std::move(message));
  }
  NotifyCloseOnce(ec);
}

void WebSocketClientTask::BeginReadLoop() {
  if (!opened_ || cancelled_) {
    return;
  }

  auto self = shared_from_this();
  if (ws_stream_ != nullptr) {
    PlainWebSocketStream* stream = ws_stream_.get();
    stream->async_read(
        ws_read_buffer_,
        [self, stream](boost::system::error_code ec, std::size_t) {
          if (self->cancelled_) {
            self->NotifyCloseOnce(boost::system::error_code{});
            return;
          }
          if (ec) {
            if (ec == websocket::error::closed) {
              self->opened_ = false;
              self->cancelled_ = true;
              self->NotifyCloseOnce(boost::system::error_code{});
              return;
            }
            self->FailAndClose(ec, "websocket client read failed");
            return;
          }

          WebSocketMessage message;
          message.binary = stream->got_binary();
          message.payload =
              boost::beast::buffers_to_string(self->ws_read_buffer_.data());
          self->ws_read_buffer_.consume(self->ws_read_buffer_.size());
          self->NotifyReadMessage(std::move(message));
          self->BeginReadLoop();
        });
    return;
  }
  if (wss_stream_ != nullptr) {
    SecureWebSocketStream* stream = wss_stream_.get();
    stream->async_read(
        ws_read_buffer_,
        [self, stream](boost::system::error_code ec, std::size_t) {
          if (self->cancelled_) {
            self->NotifyCloseOnce(boost::system::error_code{});
            return;
          }
          if (ec) {
            if (ec == websocket::error::closed) {
              self->opened_ = false;
              self->cancelled_ = true;
              self->NotifyCloseOnce(boost::system::error_code{});
              return;
            }
            self->FailAndClose(ec, "websocket client read failed");
            return;
          }

          WebSocketMessage message;
          message.binary = stream->got_binary();
          message.payload =
              boost::beast::buffers_to_string(self->ws_read_buffer_.data());
          self->ws_read_buffer_.consume(self->ws_read_buffer_.size());
          self->NotifyReadMessage(std::move(message));
          self->BeginReadLoop();
        });
  }
}

}  // namespace bsrvcore
