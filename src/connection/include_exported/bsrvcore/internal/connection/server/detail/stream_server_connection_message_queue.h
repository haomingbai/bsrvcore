/**
 * @file stream_server_connection_message_queue.h
 * @brief Message queue implementation for StreamServerConnectionImpl.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * This header defines the nested MessageQueue class of
 * StreamServerConnectionImpl. It is included by
 * stream_server_connection_impl.h
 * after the outer template class definition, so the nested class keeps access
 * to private members of the outer class (e.g. stream_).
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_CONNECTION_SERVER_STREAM_SERVER_CONNECTION_MESSAGE_QUEUE_H_
#define BSRVCORE_INTERNAL_CONNECTION_SERVER_STREAM_SERVER_CONNECTION_MESSAGE_QUEUE_H_

#include <algorithm>
#include <atomic>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/http.hpp>
#include <condition_variable>
#include <cstddef>
#include <cstdint>  // NOLINT(misc-include-cleaner): Boost.Beast http headers require std::uint32_t on some toolchains.
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

template <ValidStream S>
class StreamServerConnectionImpl<S>::MessageQueue
    : public std::enable_shared_from_this<
          typename StreamServerConnectionImpl<S>::MessageQueue> {
 public:
  explicit MessageQueue(std::weak_ptr<StreamServerConnectionImpl<S>> conn_wp)
      : conn_wp_(std::move(conn_wp)),

        queue_size_(0),
        connection_dead_(false) {}

  // Add a body chunk to be flushed.
  //
  // Threading: may be called from arbitrary threads; internally posts work to
  // the connection executor/strand.
  void AddBody(std::string body, std::size_t write_expiry) {
    BodyMessage message{AllocateShared<std::string>(std::move(body)),
                        write_expiry};
    auto self_sp = this->shared_from_this();

    if (auto conn_sp = conn_wp_.lock()) {
      boost::asio::dispatch(
          conn_sp->GetExecutor(),
          [conn_sp, self_sp, message = std::move(message)]() mutable {
            self_sp->EnqueueBodyOnStrand(std::move(message));
          });
    } else {
      MarkConnectionDeadAndNotify();
    }
  }

  // Add a response header to be flushed.
  //
  // Threading: may be called from arbitrary threads; internally posts work to
  // the connection executor/strand.
  void AddHeader(HttpResponseHeader header, std::size_t write_expiry) {
    HeaderMessage message{std::move(header), write_expiry};
    auto self_sp = this->shared_from_this();

    if (auto conn_sp = conn_wp_.lock()) {
      boost::asio::dispatch(
          conn_sp->GetExecutor(),
          [conn_sp, self_sp, message = std::move(message)]() mutable {
            self_sp->EnqueueHeaderOnStrand(std::move(message));
          });
    } else {
      MarkConnectionDeadAndNotify();
    }
  }

  // Wait until the queue becomes empty OR the connection is marked dead.
  //
  // Used by SessionMap/connection teardown paths to ensure outstanding flushes
  // are drained.
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
    std::size_t write_expiry{};
  };

  struct HeaderMessage {
    std::shared_ptr<HttpEmptyResponse> resp_sp;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    HttpEmptyResponseSerializer sr;
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    std::size_t write_expiry;

    explicit HeaderMessage(HttpResponseHeader&& header, std::size_t expiry)
        : resp_sp(AllocateShared<HttpEmptyResponse>(std::move(header))),
          sr(*resp_sp),
          write_expiry(expiry) {}
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
    if (queue_.empty() || is_writing_) {
      return;
    }
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
    auto conn_sp = conn_wp_.lock();
    if (!conn_sp || !conn_sp->IsServerRunning() ||
        !conn_sp->IsStreamAvailable()) {
      HandleConnectionUnavailable();
      return;
    }

    std::size_t batch_count = 0;
    std::size_t total_size = 0;
    std::size_t write_expiry = task.write_expiry;
    for (const auto& item : queue_) {
      if (!std::holds_alternative<BodyMessage>(item)) {
        break;
      }
      const auto& body = std::get<BodyMessage>(item);
      ++batch_count;
      total_size += body.msg->size();
      if (body.write_expiry > write_expiry) {
        write_expiry = body.write_expiry;
      }
    }

    if (batch_count == 0u) {
      HandleConnectionUnavailable();
      return;
    }

    auto merged = AllocateShared<std::string>();
    merged->reserve(total_size);

    std::size_t appended = 0;
    for (const auto& item : queue_) {
      if (appended >= batch_count) {
        break;
      }
      const auto& body = std::get<BodyMessage>(item);
      merged->append(*body.msg);
      ++appended;
    }

    auto buf = boost::asio::buffer(*merged);

    conn_sp->ArmTimeout(write_expiry);
    boost::asio::async_write(
        conn_sp->stream_, buf,
        boost::asio::bind_executor(
            conn_sp->GetExecutor(),
            [conn_sp, mq = this->shared_from_this(), merged, batch_count](
                boost::system::error_code ec, [[maybe_unused]] std::size_t) {
              mq->HandleBodyWriteComplete(ec, batch_count);
            }));
  }

  void StartWriteHeader(HeaderMessage& task) {
    auto conn_sp = conn_wp_.lock();
    if (!conn_sp || !conn_sp->IsServerRunning() ||
        !conn_sp->IsStreamAvailable()) {
      HandleConnectionUnavailable();
      return;
    }

    // Keep resp_sp alive by capturing it.
    auto resp_keeper = task.resp_sp;

    conn_sp->ArmTimeout(task.write_expiry);
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
  void HandleBodyWriteComplete(const boost::system::error_code& ec,
                               std::size_t batch_count) {
    if (auto conn_sp = conn_wp_.lock()) {
      conn_sp->CancelTimeout();
    }
    if (ec) {
      HandleWriteError(ec);
      return;
    }
    PopFrontNAndNotifyIfEmpty(batch_count);
    is_writing_ = false;
    if (!queue_.empty()) {
      StartWriteIfNeeded();
    }
  }

  void HandleHeaderWriteComplete(const boost::system::error_code& ec) {
    if (auto conn_sp = conn_wp_.lock()) {
      conn_sp->CancelTimeout();
    }
    if (ec) {
      HandleWriteError(ec);
      return;
    }
    PopFrontAndNotifyIfEmpty();
    is_writing_ = false;
    if (!queue_.empty()) {
      StartWriteIfNeeded();
    }
  }

  // ---- small helpers ----
  void PopFrontAndNotifyIfEmpty() {
    queue_.pop_front();
    auto prev = queue_size_.fetch_sub(1, std::memory_order_relaxed);
    if (prev == 1) {
      std::scoped_lock const lk(cv_mutex_);
      cv_.notify_all();
    }
  }

  void PopFrontNAndNotifyIfEmpty(std::size_t count) {
    if (count == 0u) {
      return;
    }

    const std::size_t actual = std::min(count, queue_.size());
    for (std::size_t i = 0; i < actual; ++i) {
      queue_.pop_front();
    }

    const auto prev = queue_size_.fetch_sub(actual, std::memory_order_relaxed);
    if (prev == actual) {
      std::scoped_lock const lk(cv_mutex_);
      cv_.notify_all();
    }
  }

  void HandleWriteError(const boost::system::error_code& /*ec*/) {
    if (auto conn_sp = conn_wp_.lock()) {
      conn_sp->DoClose();
    }
    connection_dead_.store(true, std::memory_order_release);
    std::scoped_lock const lk(cv_mutex_);
    cv_.notify_all();
    is_writing_ = false;
  }

  void HandleConnectionUnavailable() {
    connection_dead_.store(true, std::memory_order_release);
    std::scoped_lock const lk(cv_mutex_);
    cv_.notify_all();
    is_writing_ = false;
  }

  void MarkConnectionDeadAndNotify() {
    connection_dead_.store(true, std::memory_order_release);
    std::scoped_lock const lk(cv_mutex_);
    cv_.notify_all();
  }

  // ---- members ----
  AllocatedDeque<std::variant<BodyMessage, HeaderMessage>> queue_;
  std::weak_ptr<StreamServerConnectionImpl<S>> conn_wp_;
  bool is_writing_{false};

  std::mutex cv_mutex_;
  std::condition_variable cv_;
  std::atomic_size_t queue_size_;
  std::atomic<bool> connection_dead_;
};

#endif  // BSRVCORE_INTERNAL_CONNECTION_SERVER_STREAM_SERVER_CONNECTION_MESSAGE_QUEUE_H_
