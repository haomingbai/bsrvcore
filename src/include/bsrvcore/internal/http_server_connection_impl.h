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

#include "bsrvcore/http_server.h"
#ifndef BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_IMPL_H_
#define BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_IMPL_H_

#include <atomic>
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
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <variant>

#include "bsrvcore/http_server_task.h"
#include "bsrvcore/internal/http_server_connection.h"

namespace bsrvcore {
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
  static std::shared_ptr<HttpServerConnectionImpl<S>> Create(Args&&... args) {
    auto conn = std::make_shared<HttpServerConnectionImpl<S>>(
        std::forward<Args>(args)...);
    // Now conn is owned by shared_ptr; create message_queue_ with weak ref.
    conn->message_queue_ =
        std::make_shared<typename HttpServerConnectionImpl<S>::MessageQueue>(
            conn->weak_from_this());
    return conn;
  }

  void DoClose() override {
    if (closed_) return;
    closed_ = true;

    if (!helper::GetLowestSocket(stream_).is_open()) return;

    if constexpr (helper::IsBeastSslStream<S>::value) {
      stream_.async_shutdown(boost::asio::bind_executor(
          GetExecutor(), [self = this->shared_from_this(), this](auto ec) {
            boost::system::error_code socket_ec;
            helper::GetLowestSocket(stream_).close(socket_ec);
          }));
    } else {
      boost::asio::post(GetStrand(), [self = this->shared_from_this(), this] {
        boost::system::error_code ec;
        helper::GetLowestSocket(stream_).shutdown(
            boost::asio::ip::tcp::socket::shutdown_both, ec);
        helper::GetLowestSocket(stream_).close(ec);
      });
    }
  }

  bool IsStreamAvailable() const noexcept override { return !closed_; }

  void DoWriteResponse(HttpResponse resp, bool keep_alive) override {
    if (!IsServerRunning() || !IsStreamAvailable()) {
      DoClose();
      return;
    }
    resp.keep_alive(keep_alive);
    resp.set(boost::beast::http::field::keep_alive,
             std::to_string(GetKeepAliveTimeout()));
    resp.prepare_payload();

    resp_ = std::move(resp);

    boost::beast::http::async_write(
        stream_, resp_,
        boost::asio::bind_executor(
            GetExecutor(), [self = this->shared_from_this(), this, keep_alive](
                               boost::system::error_code ec,
                               [[maybe_unused]] std::size_t bytes_transfered) {
              if (ec) {
                DoClose();
              } else {
                if (!keep_alive) {
                  DoClose();
                } else {
                  DoCycle();
                }
              }
            }));
  }

  void DoFlushResponseHeader(
      boost::beast::http::response_header<boost::beast::http::fields> header)
      override {
    EnsureMessageQueueCreated();
    // capture shared_ptr to message_queue_ inside AddHeader to ensure it is
    // kept.
    message_queue_->AddHeader(std::move(header));
  }

  void DoFlushResponseBody(std::string body) override {
    EnsureMessageQueueCreated();
    message_queue_->AddBody(std::move(body));
  }

 protected:
  void DoReadHeader() override {
    if (!IsServerRunning() || !IsStreamAvailable()) {
      DoClose();
      return;
    }
    boost::beast::http::async_read_header(
        stream_, GetBuffer(), *GetParser(),
        boost::asio::bind_executor(
            GetExecutor(), [self = this->shared_from_this(), this](
                               boost::system::error_code ec,
                               [[maybe_unused]] std::size_t bytes_transfered) {
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
            GetExecutor(), [self = this->shared_from_this(), this](
                               boost::system::error_code ec,
                               [[maybe_unused]] std::size_t bytes_transfered) {
              if (ec) {
                DoClose();
              } else {
                MakeHttpServerTask();
              }
            }));
  }

  void ClearMessage() override {
    if (message_queue_) {
      message_queue_->Wait();
    }
  }

 private:
  // Ensure message_queue_ is created. If already created, no-op.
  void EnsureMessageQueueCreated() {
    // If already created, nothing to do.
    if (message_queue_) return;

    // Try to create it using weak_from_this() (works only if object is owned
    // by shared_ptr). If weak_from_this is empty, message_queue_ will be
    // created later when AddBody/AddHeader runs (they call this again).
    auto weak = this->weak_from_this();
    if (auto locked = weak.lock()) {
      std::lock_guard<std::mutex> lock(mtx_);
      if (!message_queue_) {
        message_queue_ = std::make_shared<
            typename HttpServerConnectionImpl<S>::MessageQueue>(std::move(
            std::static_pointer_cast<HttpServerConnectionImpl<S>>(locked)));
      }
    }
  }

  class MessageQueue : public std::enable_shared_from_this<MessageQueue> {
   public:
    explicit MessageQueue(std::weak_ptr<HttpServerConnectionImpl<S>> conn_wp)
        : conn_wp_(std::move(conn_wp)),
          is_writing_(false),
          queue_size_(0),
          connection_dead_(false) {}

    // Public interface
    void AddBody(std::string body) {
      BodyMessage message{std::make_shared<std::string>(std::move(body))};
      auto self_sp = this->shared_from_this();

      if (auto conn_sp = conn_wp_.lock()) {
        boost::asio::post(
            conn_sp->GetExecutor(),
            [conn_sp, self_sp, message = std::move(message)]() mutable {
              self_sp->EnqueueBodyOnStrand(std::move(message));
            });
      } else {
        MarkConnectionDeadAndNotify();
      }
    }

    void AddHeader(
        boost::beast::http::response_header<boost::beast::http::fields>
            header) {
      HeaderMessage message{std::move(header)};
      auto self_sp = this->shared_from_this();

      if (auto conn_sp = conn_wp_.lock()) {
        boost::asio::post(
            conn_sp->GetExecutor(),
            [conn_sp, self_sp, message = std::move(message)]() mutable {
              self_sp->EnqueueHeaderOnStrand(std::move(message));
            });
      } else {
        MarkConnectionDeadAndNotify();
      }
    }

    // Wait until queue is empty OR connection is marked dead.
    void Wait() {
      std::unique_lock<std::mutex> lk(cv_mutex_);
      cv_.wait(lk, [this] {
        return connection_dead_.load(std::memory_order_acquire) ||
               queue_size_.load(std::memory_order_acquire) == 0;
      });
    }

   private:
    // ---- messages ----
    struct BodyMessage {
      std::shared_ptr<std::string> msg;
    };

    struct HeaderMessage {
      std::shared_ptr<
          boost::beast::http::response<boost::beast::http::empty_body>>
          resp_sp;
      boost::beast::http::response_serializer<boost::beast::http::empty_body>
          sr;

      explicit HeaderMessage(
          boost::beast::http::response_header<boost::beast::http::fields>&&
              header)
          : resp_sp(std::make_shared<boost::beast::http::response<
                        boost::beast::http::empty_body>>(std::move(header))),
            sr(*resp_sp) {}
    };

    // ---- enqueue helpers (run on strand) ----
    void EnqueueBodyOnStrand(BodyMessage message) {
      queue_.emplace_back(std::move(message));
      queue_size_.fetch_add(1, std::memory_order_relaxed);
      StartWriteIfNeeded();
    }

    void EnqueueHeaderOnStrand(HeaderMessage message) {
      queue_.emplace_back(std::move(message));
      queue_size_.fetch_add(1, std::memory_order_relaxed);
      StartWriteIfNeeded();
    }

    // ---- start write (run on strand) ----
    void StartWriteIfNeeded() {
      if (queue_.empty() || is_writing_) return;
      is_writing_ = true;

      auto& front = queue_.front();
      std::visit(
          [mq = this->shared_from_this()](auto& task) {
            using T = std::decay_t<decltype(task)>;
            if constexpr (std::is_same_v<T, BodyMessage>) {
              mq->StartWriteBody(task);
            } else {
              mq->StartWriteHeader(task);
            }
          },
          front);
    }

    // ---- write starters ----
    void StartWriteBody(BodyMessage& task) {
      auto msg_sp = task.msg;
      auto buf = boost::asio::buffer(*msg_sp);

      auto conn_sp = conn_wp_.lock();
      if (!conn_sp || !conn_sp->IsServerRunning() ||
          !conn_sp->IsStreamAvailable()) {
        HandleConnectionUnavailable();
        return;
      }

      boost::asio::async_write(
          conn_sp->stream_, buf,
          boost::asio::bind_executor(
              conn_sp->GetExecutor(),
              [conn_sp, mq = this->shared_from_this(), msg_sp](
                  boost::system::error_code ec, [[maybe_unused]] std::size_t) {
                mq->HandleBodyWriteComplete(ec);
              }));
    }

    void StartWriteHeader(HeaderMessage& task) {
      auto conn_sp = conn_wp_.lock();
      if (!conn_sp || !conn_sp->IsServerRunning() ||
          !conn_sp->IsStreamAvailable()) {
        HandleConnectionUnavailable();
        return;
      }

      // keep resp_sp alive by capturing it
      auto resp_keeper = task.resp_sp;

      boost::beast::http::async_write_header(
          conn_sp->stream_, task.sr,
          boost::asio::bind_executor(
              conn_sp->GetExecutor(),
              [conn_sp, mq = this->shared_from_this(), resp_keeper](
                  boost::system::error_code ec, [[maybe_unused]] std::size_t) {
                mq->HandleHeaderWriteComplete(ec);
              }));
    }

    // ---- completion handlers (run on strand) ----
    void HandleBodyWriteComplete(const boost::system::error_code& ec) {
      if (ec) {
        HandleWriteError(ec);
        return;
      }
      PopFrontAndNotifyIfEmpty();
      is_writing_ = false;
      if (!queue_.empty()) StartWriteIfNeeded();
    }

    void HandleHeaderWriteComplete(const boost::system::error_code& ec) {
      if (ec) {
        HandleWriteError(ec);
        return;
      }
      PopFrontAndNotifyIfEmpty();
      is_writing_ = false;
      if (!queue_.empty()) StartWriteIfNeeded();
    }

    // ---- small helpers ----
    void PopFrontAndNotifyIfEmpty() {
      queue_.pop_front();
      auto prev = queue_size_.fetch_sub(1, std::memory_order_relaxed);
      if (prev == 1) {
        std::lock_guard<std::mutex> lk(cv_mutex_);
        cv_.notify_all();
      }
    }

    void HandleWriteError(const boost::system::error_code& /*ec*/) {
      if (auto conn_sp = conn_wp_.lock()) conn_sp->DoClose();
      connection_dead_.store(true, std::memory_order_release);
      std::lock_guard<std::mutex> lk(cv_mutex_);
      cv_.notify_all();
      is_writing_ = false;
    }

    void HandleConnectionUnavailable() {
      connection_dead_.store(true, std::memory_order_release);
      std::lock_guard<std::mutex> lk(cv_mutex_);
      cv_.notify_all();
      is_writing_ = false;
    }

    void MarkConnectionDeadAndNotify() {
      connection_dead_.store(true, std::memory_order_release);
      std::lock_guard<std::mutex> lk(cv_mutex_);
      cv_.notify_all();
    }

    // ---- members ----
    std::deque<std::variant<BodyMessage, HeaderMessage>> queue_;
    std::weak_ptr<HttpServerConnectionImpl<S>> conn_wp_;
    bool is_writing_;

    std::mutex cv_mutex_;
    std::condition_variable cv_;
    std::atomic_size_t queue_size_;
    std::atomic<bool> connection_dead_;
  };

  // members
  boost::beast::http::response<boost::beast::http::string_body> resp_;
  S stream_;
  std::mutex mtx_;
  std::shared_ptr<MessageQueue> message_queue_;  // now shared
  std::atomic<bool> closed_;
};

}  // namespace connection_internal
}  // namespace bsrvcore

#endif  // BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_IMPL_H_
