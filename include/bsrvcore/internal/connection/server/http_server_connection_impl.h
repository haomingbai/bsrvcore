/**
 * @file http_server_connection_impl.h
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

#ifndef BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_IMPL_H_
#define BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_IMPL_H_

#include <atomic>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/ssl.hpp>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <variant>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/internal/connection/server/http_server_connection.h"

namespace bsrvcore {

class HttpServer;

namespace connection_internal {

namespace helper {

template <typename T>
struct IsBeastSslStream : std::false_type {};

template <typename NextLayer>
struct IsBeastSslStream<boost::beast::ssl_stream<NextLayer>> : std::true_type {
};

inline boost::asio::ip::tcp::socket& GetLowestSocket(
    boost::beast::tcp_stream& s) {
  return s.socket();
}

inline boost::asio::ip::tcp::socket& GetLowestSocket(
    boost::beast::ssl_stream<boost::beast::tcp_stream>& s) {
  return s.next_layer().socket();
}

}  // namespace helper

template <typename S>
concept ValidStream = requires(S s) {
  { helper::GetLowestSocket(s) };
};

template <ValidStream S>
class HttpServerConnectionImpl : public HttpServerConnection {
 public:
  class MessageQueue;

  // Keep original constructor signature for compatibility.
  HttpServerConnectionImpl(
      S stream, boost::asio::strand<boost::asio::any_io_executor> strand,
      HttpServer* srv, std::size_t header_read_expiry,
      std::size_t keep_alive_timeout)
      : HttpServerConnection(std::move(strand), srv, header_read_expiry,
                             keep_alive_timeout),
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

    boost::asio::post(
        GetStrand(),
        boost::asio::bind_allocator(
            GetHandlerAllocator(), [self = this->shared_from_this(), this] {
              if (!helper::GetLowestSocket(stream_).is_open()) {
                return;
              }

              if constexpr (helper::IsBeastSslStream<S>::value) {
                stream_.async_shutdown(boost::asio::bind_executor(
                    GetExecutor(),
                    boost::asio::bind_allocator(
                        GetHandlerAllocator(),
                        [self,
                         this]([[maybe_unused]] boost::system::error_code ec) {
                          boost::system::error_code socket_ec;
                          helper::GetLowestSocket(stream_).close(socket_ec);
                        })));
              } else {
                boost::system::error_code ec;
                helper::GetLowestSocket(stream_).shutdown(
                    boost::asio::ip::tcp::socket::shutdown_both, ec);
                helper::GetLowestSocket(stream_).close(ec);
              }
            }));
  }

  bool IsStreamAvailable() const noexcept override { return !closed_; }

  void DoWriteResponse(HttpResponse resp, bool keep_alive) override {
    if (!IsServerRunning() || !IsStreamAvailable()) {
      DoClose();
      return;
    }

    boost::asio::dispatch(
        GetStrand(),
        boost::asio::bind_allocator(
            GetHandlerAllocator(),
            [self = this->shared_from_this(), this, keep_alive,
             response = std::move(resp)]() mutable {
              if (!IsServerRunning() || !IsStreamAvailable()) {
                DoClose();
                return;
              }

              response.keep_alive(keep_alive);
              response.set(boost::beast::http::field::keep_alive,
                           std::to_string(GetKeepAliveTimeout()));
              response.prepare_payload();
              resp_ = std::move(response);

              boost::beast::http::async_write(
                  stream_, resp_,
                  boost::asio::bind_executor(
                      GetExecutor(),
                      boost::asio::bind_allocator(
                          GetHandlerAllocator(),
                          [self, this, keep_alive](
                              boost::system::error_code ec,
                              [[maybe_unused]] std::size_t bytes_transfered) {
                            if (ec) {
                              DoClose();
                            } else if (!keep_alive) {
                              DoClose();
                            } else {
                              DoCycle();
                            }
                          })));
            }));
  }

  void DoFlushResponseHeader(
      boost::beast::http::response_header<boost::beast::http::fields> header)
      override;

  void DoFlushResponseBody(std::string body) override;

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
            boost::asio::bind_allocator(
                GetHandlerAllocator(),
                [self = this->shared_from_this(), this](
                    boost::system::error_code ec,
                    [[maybe_unused]] std::size_t bytes_transfered) {
                  if (ec) {
                    DoClose();
                  } else {
                    DoRoute();
                  }
                })));
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
            boost::asio::bind_allocator(
                GetHandlerAllocator(),
                [self = this->shared_from_this(), this](
                    boost::system::error_code ec,
                    [[maybe_unused]] std::size_t bytes_transfered) {
                  if (ec) {
                    DoClose();
                  } else {
                    DoForwardRequest();
                  }
                })));
  }

  void ClearMessage() override;

 private:
  // Ensure message_queue_ is created. If already created, no-op.
  void EnsureMessageQueueCreated();

  // members
  boost::beast::http::response<boost::beast::http::string_body> resp_;
  S stream_;
  std::mutex mtx_;
  std::shared_ptr<MessageQueue> message_queue_;  // now shared
  std::atomic<bool> closed_;
};

// Keep the MessageQueue definition out of the main header to reduce size.
// It remains a nested class (HttpServerConnectionImpl<S>::MessageQueue), so
// it keeps access to the outer class' private members.
#include "bsrvcore/internal/connection/server/http_server_connection_message_queue.h"

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
    boost::beast::http::response_header<boost::beast::http::fields> header) {
  EnsureMessageQueueCreated();
  // Capture shared_ptr to message_queue_ inside AddHeader to keep it alive.
  message_queue_->AddHeader(std::move(header));
}

template <ValidStream S>
void HttpServerConnectionImpl<S>::DoFlushResponseBody(std::string body) {
  EnsureMessageQueueCreated();
  message_queue_->AddBody(std::move(body));
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
  if (message_queue_) return;

  // Try to create it using weak_from_this() (works only if object is owned
  // by shared_ptr). If weak_from_this is empty, message_queue_ will be
  // created later when AddBody/AddHeader runs (they call this again).
  auto weak = this->weak_from_this();
  if (auto locked = weak.lock()) {
    std::lock_guard<std::mutex> lock(mtx_);
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

#endif  // BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_IMPL_H_
