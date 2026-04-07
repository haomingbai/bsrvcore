/**
 * @file stream_server_connection_impl.h
 * @brief Template implementation for HTTP server connections with multiple
 * stream types
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-01
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Template-based implementation of HTTP server connections supporting
 * both plain TCP and SSL streams. Provides efficient message queuing
 * and asynchronous I/O operations for high-performance HTTP serving.
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_CONNECTION_SERVER_STREAM_SERVER_CONNECTION_IMPL_H_
#define BSRVCORE_INTERNAL_CONNECTION_SERVER_STREAM_SERVER_CONNECTION_IMPL_H_

#include <atomic>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>  // NOLINT(misc-include-cleaner): Boost.Beast http headers require std::uint32_t on some toolchains.
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/internal/connection/server/stream_server_connection.h"

namespace bsrvcore {

class HttpServer;

namespace connection_internal {

namespace helper {

template <typename T>
struct IsBeastSslStream : std::false_type {};

template <typename NextLayer>
struct IsBeastSslStream<BasicSslStream<NextLayer>> : std::true_type {};

inline Tcp::socket& GetLowestSocket(TcpStream& s) { return s.socket(); }

inline Tcp::socket& GetLowestSocket(SslStream& s) {
  return s.next_layer().socket();
}

template <typename NextLayer>
inline Tcp::socket& GetLowestSocket(BasicWebSocketStream<NextLayer>& s) {
  return GetLowestSocket(s.next_layer());
}

inline bool IsWebSocketReservedHeader(HttpField field) {
  switch (field) {
    case HttpField::connection:
    case HttpField::upgrade:
    case HttpField::sec_websocket_accept:
    case HttpField::sec_websocket_extensions:
    case HttpField::sec_websocket_key:
    case HttpField::sec_websocket_protocol:
    case HttpField::sec_websocket_version:
      return true;
    default:
      return false;
  }
}

}  // namespace helper

template <typename S>
concept ValidStream = requires(S s) {
  { helper::GetLowestSocket(s) };
};

template <ValidStream S>
class HttpServerConnectionImpl : public StreamServerConnection {
 public:
  using UpgradeWebSocketStream = BasicWebSocketStream<S>;
  class MessageQueue;

  // Constructor receives shared server runtime controls from accept path.
  HttpServerConnectionImpl(S stream, IoExecutor io_executor, HttpServer* srv,
                           std::size_t header_read_expiry,
                           std::size_t keep_alive_timeout,
                           bool has_max_connection,
                           std::atomic<std::int64_t>* available_connection_num,
                           std::size_t endpoint_index)
      : StreamServerConnection(std::move(io_executor), srv, header_read_expiry,
                               keep_alive_timeout, has_max_connection,
                               available_connection_num, endpoint_index),
        stream_(std::move(stream)),
        closed_(false) {
    // Do NOT create message_queue_ here by calling shared_from_this(),
    // because object might not yet be owned by shared_ptr.
    //
    // Prefer using the static Create(...) factory which constructs the
    // shared_ptr and then attaches the message_queue_ immediately:
    //   auto conn = HttpServerConnectionImpl<Stream>::Create(...);
  }

  // Preferred factory: ensures connection is owned by shared_ptr and then
  // creates message_queue_ which stores a weak_ptr back to the connection.
  template <typename... Args>
  static std::shared_ptr<HttpServerConnectionImpl<S>> Create(Args&&... args);

  void DoClose() override {
    if (closed_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }

    boost::asio::dispatch(
        GetExecutor(), [self = this->shared_from_this(), this] {
          auto& socket = ws_stream_ != nullptr
                             ? helper::GetLowestSocket(*ws_stream_)
                             : helper::GetLowestSocket(stream_);
          if (!socket.is_open()) {
            return;
          }

          if (ws_stream_ != nullptr) {
            boost::system::error_code ec;
            socket.shutdown(Tcp::socket::shutdown_both, ec);
            socket.close(ec);
          } else if constexpr (helper::IsBeastSslStream<S>::value) {
            stream_.async_shutdown(boost::asio::bind_executor(
                GetExecutor(),
                [self, this]([[maybe_unused]] boost::system::error_code ec) {
                  boost::system::error_code socket_ec;
                  helper::GetLowestSocket(stream_).close(socket_ec);
                }));
          } else {
            boost::system::error_code ec;
            helper::GetLowestSocket(stream_).shutdown(
                Tcp::socket::shutdown_both, ec);
            helper::GetLowestSocket(stream_).close(ec);
          }
        });
  }

  bool IsStreamAvailable() const noexcept override { return !closed_; }

  void DoWriteResponse(HttpResponse resp, bool keep_alive,
                       std::size_t write_expiry) override {
    if (!IsServerRunning() || !IsStreamAvailable()) {
      DoClose();
      return;
    }

    boost::asio::dispatch(
        GetExecutor(), [self = this->shared_from_this(), this, keep_alive,
                        write_expiry, response = std::move(resp)]() mutable {
          if (!IsServerRunning() || !IsStreamAvailable()) {
            DoClose();
            return;
          }

          ArmTimeout(write_expiry);

          response.keep_alive(keep_alive);
          response.set(HttpField::keep_alive,
                       std::to_string(GetKeepAliveTimeout()));
          response.prepare_payload();
          resp_ = std::move(response);

          boost::beast::http::async_write(
              stream_, resp_,
              boost::asio::bind_executor(
                  GetExecutor(),
                  [self, this, keep_alive](boost::system::error_code ec,
                                           [[maybe_unused]] std::size_t) {
                    CancelTimeout();
                    if (ec || !keep_alive) {
                      DoClose();
                    } else {
                      DoCycle();
                    }
                  }));
        });
  }

  void DoFlushResponseHeader(HttpResponseHeader header,
                             std::size_t write_expiry) override;

  void DoFlushResponseBody(std::string body, std::size_t write_expiry) override;

  void DoWebSocketAccept(HttpRequest req, HttpResponseHeader response_header,
                         WebSocketAcceptCallback callback) override;
  void DoWebSocketRead(WebSocketReadCallback callback) override;
  void DoWebSocketWrite(std::string payload, bool binary,
                        WebSocketWriteCallback callback) override;
  void DoWebSocketControl(WebSocketControlKind kind, std::string payload,
                          WebSocketWriteCallback callback) override;
  void DoWebSocketClose(WebSocketWriteCallback callback) override;

 protected:
  void DoReadHeader() override {
    if (!IsServerRunning() || !IsStreamAvailable()) {
      DoClose();
      return;
    }
    boost::beast::http::async_read_header(
        stream_, GetBuffer(), *GetParser(),
        boost::asio::bind_executor(
            GetExecutor(),
            [self = this->shared_from_this(), this](
                boost::system::error_code ec, [[maybe_unused]] std::size_t) {
              if (ec) {
                DoClose();
              } else {
                DoRoute();
              }
            }));
  }

  void DoReadBody() override {
    if (!IsServerRunning() || !IsStreamAvailable()) {
      DoClose();
      return;
    }
    boost::beast::http::async_read(
        stream_, GetBuffer(), *GetParser(),
        boost::asio::bind_executor(
            GetExecutor(),
            [self = this->shared_from_this(), this](
                boost::system::error_code ec, [[maybe_unused]] std::size_t) {
              if (ec) {
                DoClose();
              } else {
                DoForwardRequest();
              }
            }));
  }

  void ClearMessage() override;

 private:
  // Ensure message_queue_ is created. If already created, no-op.
  void EnsureMessageQueueCreated();
  void DoWebSocketPingControl(std::string payload,
                              WebSocketWriteCallback callback);
  void DoWebSocketPongControl(std::string payload,
                              WebSocketWriteCallback callback);
  void DoWebSocketCloseControl(std::string payload,
                               WebSocketWriteCallback callback);

  // members
  HttpResponse resp_;
  S stream_;
  std::mutex mtx_;
  std::shared_ptr<MessageQueue> message_queue_;  // now shared
  std::unique_ptr<UpgradeWebSocketStream> ws_stream_;
  std::atomic<bool> closed_;
};

// Keep the MessageQueue definition out of the main header to reduce size.
// It remains a nested class (HttpServerConnectionImpl<S>::MessageQueue), so
// it keeps access to the outer class' private members.
#include "bsrvcore/internal/connection/server/detail/stream_server_connection_message_queue.h"

template <ValidStream S>
template <typename... Args>
std::shared_ptr<HttpServerConnectionImpl<S>>
HttpServerConnectionImpl<S>::Create(Args&&... args) {
  auto conn =
      AllocateShared<HttpServerConnectionImpl<S>>(std::forward<Args>(args)...);
  // Now conn is owned by shared_ptr; create message_queue_ with weak ref.
  conn->message_queue_ =
      AllocateShared<typename HttpServerConnectionImpl<S>::MessageQueue>(
          std::weak_ptr<HttpServerConnectionImpl<S>>(conn));
  return conn;
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::DoFlushResponseHeader(
    HttpResponseHeader header, std::size_t write_expiry) {
  EnsureMessageQueueCreated();
  // Capture shared_ptr to message_queue_ inside AddHeader to keep it alive.
  message_queue_->AddHeader(std::move(header), write_expiry);
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::DoFlushResponseBody(
    std::string body, std::size_t write_expiry) {
  EnsureMessageQueueCreated();
  message_queue_->AddBody(std::move(body), write_expiry);
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::DoWebSocketAccept(
    HttpRequest req, HttpResponseHeader response_header,
    WebSocketAcceptCallback callback) {
  if (!IsServerRunning() || !IsStreamAvailable()) {
    callback(make_error_code(boost::system::errc::not_connected));
    return;
  }

  boost::asio::dispatch(
      GetExecutor(),
      [self = this->shared_from_this(), this, request = std::move(req),
       response_header = std::move(response_header),
       callback = std::move(callback)]() mutable {
        if (!IsServerRunning() || !IsStreamAvailable()) {
          callback(make_error_code(boost::system::errc::not_connected));
          return;
        }

        if (ws_stream_ != nullptr) {
          callback(make_error_code(boost::system::errc::already_connected));
          return;
        }

        ws_stream_ =
            std::make_unique<UpgradeWebSocketStream>(std::move(stream_));
        ws_stream_->set_option(
            boost::beast::websocket::stream_base::timeout::suggested(
                boost::beast::role_type::server));
        ws_stream_->set_option(boost::beast::websocket::stream_base::decorator(
            [response_header =
                 std::move(response_header)](WebSocketResponse& response) {
              for (const auto& field : response_header) {
                if (field.name() == HttpField::unknown ||
                    helper::IsWebSocketReservedHeader(field.name())) {
                  continue;
                }
                response.set(field.name_string(), field.value());
              }
            }));

        ArmTimeout(GetKeepAliveTimeout() * 1000);
        ws_stream_->async_accept(
            std::move(request),
            boost::asio::bind_executor(
                GetExecutor(), [self, this, callback = std::move(callback)](
                                   boost::system::error_code ec) mutable {
                  CancelTimeout();
                  if (ec) {
                    DoClose();
                  }
                  callback(ec);
                }));
      });
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::DoWebSocketRead(
    WebSocketReadCallback callback) {
  if (!IsServerRunning() || !IsStreamAvailable()) {
    callback(make_error_code(boost::system::errc::not_connected),
             WebSocketMessage{});
    return;
  }

  boost::asio::dispatch(GetExecutor(), [self = this->shared_from_this(), this,
                                        callback =
                                            std::move(callback)]() mutable {
    if (ws_stream_ == nullptr || !IsServerRunning() || !IsStreamAvailable()) {
      callback(make_error_code(boost::system::errc::not_connected),
               WebSocketMessage{});
      return;
    }

    constexpr std::size_t kWebSocketReadReserveBytes = 16 * 1024;
    ReserveReadBuffer(kWebSocketReadReserveBytes);
    ArmTimeout(GetKeepAliveTimeout() * 1000);
    ws_stream_->async_read(
        GetBuffer(),
        boost::asio::bind_executor(
            GetExecutor(),
            [self, this, callback = std::move(callback)](
                boost::system::error_code ec, std::size_t) mutable {
              CancelTimeout();
              WebSocketMessage message;
              if (!ec) {
                message.binary = ws_stream_->got_binary();
                message.payload =
                    boost::beast::buffers_to_string(GetBuffer().data());
                ClearReadBuffer();
              }
              callback(ec, std::move(message));
            }));
  });
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::DoWebSocketWrite(
    std::string payload, bool binary, WebSocketWriteCallback callback) {
  if (!IsServerRunning() || !IsStreamAvailable()) {
    callback(make_error_code(boost::system::errc::not_connected));
    return;
  }

  boost::asio::dispatch(GetExecutor(), [self = this->shared_from_this(), this,
                                        payload = std::move(payload), binary,
                                        callback =
                                            std::move(callback)]() mutable {
    if (ws_stream_ == nullptr || !IsServerRunning() || !IsStreamAvailable()) {
      callback(make_error_code(boost::system::errc::not_connected));
      return;
    }

    auto payload_sp = AllocateShared<std::string>(std::move(payload));
    ws_stream_->binary(binary);
    ArmTimeout(GetKeepAliveTimeout() * 1000);
    ws_stream_->async_write(
        boost::asio::buffer(*payload_sp),
        boost::asio::bind_executor(
            GetExecutor(),
            [self, this, payload_sp, callback = std::move(callback)](
                boost::system::error_code ec, std::size_t) mutable {
              CancelTimeout();
              callback(ec);
            }));
  });
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::DoWebSocketControl(
    WebSocketControlKind kind, std::string payload,
    WebSocketWriteCallback callback) {
  if (!IsServerRunning() || !IsStreamAvailable()) {
    callback(make_error_code(boost::system::errc::not_connected));
    return;
  }

  boost::asio::dispatch(GetExecutor(), [self = this->shared_from_this(), this,
                                        kind, payload = std::move(payload),
                                        callback =
                                            std::move(callback)]() mutable {
    if (ws_stream_ == nullptr || !IsServerRunning() || !IsStreamAvailable()) {
      callback(make_error_code(boost::system::errc::not_connected));
      return;
    }

    ArmTimeout(GetKeepAliveTimeout() * 1000);
    switch (kind) {
      case WebSocketControlKind::kPing:
        DoWebSocketPingControl(std::move(payload), std::move(callback));
        return;
      case WebSocketControlKind::kPong:
        DoWebSocketPongControl(std::move(payload), std::move(callback));
        return;
      case WebSocketControlKind::kClose:
        DoWebSocketCloseControl(std::move(payload), std::move(callback));
        return;
    }
  });
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::DoWebSocketPingControl(
    std::string payload, WebSocketWriteCallback callback) {
  ws_stream_->async_ping(
      WebSocketPingData(std::move(payload)),
      boost::asio::bind_executor(GetExecutor(),
                                 [self = this->shared_from_this(), this,
                                  callback = std::move(callback)](
                                     boost::system::error_code ec) mutable {
                                   CancelTimeout();
                                   callback(ec);
                                 }));
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::DoWebSocketPongControl(
    std::string payload, WebSocketWriteCallback callback) {
  ws_stream_->async_pong(
      WebSocketPingData(std::move(payload)),
      boost::asio::bind_executor(GetExecutor(),
                                 [self = this->shared_from_this(), this,
                                  callback = std::move(callback)](
                                     boost::system::error_code ec) mutable {
                                   CancelTimeout();
                                   callback(ec);
                                 }));
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::DoWebSocketCloseControl(
    std::string payload, WebSocketWriteCallback callback) {
  ws_stream_->async_close(
      WebSocketCloseReason(std::move(payload)),
      boost::asio::bind_executor(GetExecutor(),
                                 [self = this->shared_from_this(), this,
                                  callback = std::move(callback)](
                                     boost::system::error_code ec) mutable {
                                   CancelTimeout();
                                   callback(ec);
                                 }));
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::DoWebSocketClose(
    WebSocketWriteCallback callback) {
  if (!IsStreamAvailable()) {
    callback(boost::system::error_code{});
    return;
  }

  boost::asio::dispatch(
      GetExecutor(), [self = this->shared_from_this(), this,
                      callback = std::move(callback)]() mutable {
        if (ws_stream_ == nullptr) {
          DoClose();
          callback(boost::system::error_code{});
          return;
        }

        ws_stream_->async_close(
            boost::beast::websocket::close_code::normal,
            boost::asio::bind_executor(
                GetExecutor(), [self, this, callback = std::move(callback)](
                                   boost::system::error_code ec) mutable {
                  DoClose();
                  callback(ec);
                }));
      });
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::ClearMessage() {
  if (message_queue_) {
    message_queue_->Wait();
  }
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::EnsureMessageQueueCreated() {
  // If already created, nothing to do.
  if (message_queue_) {
    return;
  }

  // Try to create it using weak_from_this() (works only if object is owned
  // by shared_ptr). If weak_from_this is empty, message_queue_ will be
  // created later when AddBody/AddHeader runs (they call this again).
  auto weak = this->weak_from_this();
  if (auto locked = weak.lock()) {
    std::scoped_lock const lock(mtx_);
    if (!message_queue_) {
      auto conn_sp =
          std::static_pointer_cast<HttpServerConnectionImpl<S>>(locked);
      message_queue_ =
          AllocateShared<typename HttpServerConnectionImpl<S>::MessageQueue>(
              std::weak_ptr<HttpServerConnectionImpl<S>>(conn_sp));
    }
  }
}

}  // namespace connection_internal
}  // namespace bsrvcore

#endif  // BSRVCORE_INTERNAL_CONNECTION_SERVER_STREAM_SERVER_CONNECTION_IMPL_H_
