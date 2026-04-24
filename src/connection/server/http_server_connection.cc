/**
 * @file http_server_connection.cc
 * @brief StreamServerConnection base implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-30
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements common connection orchestration for request parsing and response
 * writing.
 */

#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/system/error_code.hpp>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/internal/connection/server/stream_server_connection.h"
#include "bsrvcore/session/context.h"

using bsrvcore::FlatBuffer;
using bsrvcore::HttpRequestParser;
using bsrvcore::IoExecutor;
using bsrvcore::SteadyTimer;
using bsrvcore::StreamServerConnection;

namespace {
constexpr std::size_t kInitialReadBufferReserveBytes = 16 * 1024;
}  // namespace

void StreamServerConnection::Post(std::function<void()> fn) {
  if (srv_ == nullptr) {
    return;
  }

  srv_->Post([fn = std::move(fn)]() { fn(); });
}

void StreamServerConnection::Dispatch(std::function<void()> fn) {
  if (srv_ == nullptr) {
    return;
  }

  srv_->Dispatch([fn = std::move(fn)]() { fn(); });
}

void StreamServerConnection::PostToIoContext(std::function<void()> fn) {
  if ((srv_ == nullptr) || !IsServerRunning()) {
    return;
  }

  boost::asio::post(io_executor_, [fn = std::move(fn)]() { fn(); });
}

void StreamServerConnection::DispatchToIoContext(std::function<void()> fn) {
  if ((srv_ == nullptr) || !IsServerRunning()) {
    return;
  }

  boost::asio::dispatch(io_executor_, [fn = std::move(fn)]() { fn(); });
}

IoExecutor StreamServerConnection::GetIoExecutor() const noexcept {
  return io_executor_;
}

std::vector<IoExecutor> StreamServerConnection::GetEndpointExecutors() const {
  if ((srv_ == nullptr) || !IsServerRunning()) {
    return {};
  }
  return srv_->GetEndpointExecutors(endpoint_index_);
}

std::vector<IoExecutor> StreamServerConnection::GetGlobalExecutors() const {
  if ((srv_ == nullptr) || !IsServerRunning()) {
    return {};
  }
  return srv_->GetGlobalExecutors();
}

void StreamServerConnection::SetTimer(std::size_t timeout,
                                      std::function<void()> callback) {
  if ((srv_ == nullptr) || !IsServerRunning()) {
    return;
  }

  auto timer = AllocateShared<SteadyTimer>(io_executor_);
  timer->expires_after(std::chrono::milliseconds(timeout));
  timer->async_wait([this, callback = std::move(callback),
                     timer](boost::system::error_code ec) mutable {
    (void)timer;
    if (ec || !IsServerRunning()) {
      return;
    }
    callback();
  });
}

std::shared_ptr<bsrvcore::Context>
StreamServerConnection::GetContext() noexcept {
  return srv_->GetContext();
}

std::shared_ptr<bsrvcore::Context> StreamServerConnection::GetSession(
    const std::string& sessionid) {
  return srv_->GetSession(sessionid);
}

std::shared_ptr<bsrvcore::Context> StreamServerConnection::GetSession(
    std::string&& sessionid) {
  return srv_->GetSession(std::move(sessionid));
}

bool StreamServerConnection::SetSessionTimeout(const std::string& sessionid,
                                               std::size_t timeout) {
  return srv_->SetSessionTimeout(sessionid, timeout);
}

bool StreamServerConnection::SetSessionTimeout(std::string&& sessionid,
                                               std::size_t timeout) {
  return srv_->SetSessionTimeout(std::move(sessionid), timeout);
}

void StreamServerConnection::Log(bsrvcore::LogLevel level,
                                 std::string message) {
  srv_->Log(level, std::move(message));
}

void StreamServerConnection::Run() {
  if (!IsServerRunning() || !IsStreamAvailable()) {
    DoClose();
    return;
  }

  boost::asio::post(io_executor_, [self = shared_from_this(), this] {
    ArmTimeout(header_read_expiry_);
    DoReadHeader();
  });
}

void StreamServerConnection::DoRoute() {
  CancelTimeout();

  if (!parser_->is_header_done()) {
    DoClose();
    return;
  }

  if (!IsServerRunning() || !IsStreamAvailable()) {
    DoClose();
    return;
  }

  auto& res = parser_->get();
  auto target = res.target();

  srv_->RouteInternal(
      HttpServer::BeastHttpVerbToHttpRequestMethod(res.method()), target,
      route_result_);

  if (route_result_.handler == nullptr) {
    DoClose();
    return;
  }

  ArmTimeout(route_result_.read_expiry);

  if (route_result_.max_body_size != 0u) {
    if (res.has_content_length()) {
      std::uint64_t content_length{};
      res.content_length(content_length);
      if (content_length > route_result_.max_body_size) {
        DoClose();
      } else {
        const std::size_t reserve_bytes =
            static_cast<std::size_t>(std::min<std::uint64_t>(
                content_length,
                static_cast<std::uint64_t>(route_result_.max_body_size)));
        ReserveReadBuffer(reserve_bytes);
        ReserveRequestBodyBuffer(reserve_bytes);
      }
    }
    parser_->body_limit(route_result_.max_body_size);
  }

  DoReadBody();
}

void StreamServerConnection::DoForwardRequest() {
  if (!IsServerRunning() || !IsStreamAvailable()) {
    DoClose();
    return;
  }

  CancelTimeout();
  std::shared_ptr<HttpPreServerTask> const task = HttpPreServerTask::Create(
      parser_->release(), std::move(route_result_), shared_from_this());
  task->Start();
}

void StreamServerConnection::DoCycle() {
  ClearMessage();
  if (IsServerRunning() && IsStreamAvailable()) {
    route_result_ = {};
    parser_ = AllocateUnique<HttpRequestParser>();
    ClearReadBuffer();
    ReserveReadBuffer(kInitialReadBufferReserveBytes);
    ArmTimeout(header_read_expiry_ + keep_alive_timeout_);

    Run();
  } else {
    DoClose();
    return;
  }
}

StreamServerConnection::StreamServerConnection(
    IoExecutor io_executor, HttpServer* srv, std::size_t header_read_expiry,
    std::size_t keep_alive_timeout, bool has_max_connection,
    std::atomic<std::int64_t>* available_connection_num,
    std::size_t endpoint_index)
    : io_executor_(std::move(io_executor)),
      timer_(io_executor_),
      buf_(),
      srv_(srv),
      parser_(AllocateUnique<HttpRequestParser>()),
      header_read_expiry_(header_read_expiry),
      keep_alive_timeout_(keep_alive_timeout),
      kHasMaxConnection_(has_max_connection),
      available_connection_num_(available_connection_num),
      endpoint_index_(endpoint_index),
      handler_alloc_() {
  ReserveReadBuffer(kInitialReadBufferReserveBytes);

  if (kHasMaxConnection_ && available_connection_num_ != nullptr) {
    available_connection_num_->fetch_sub(1, std::memory_order_relaxed);
  }
}

StreamServerConnection::~StreamServerConnection() {
  if (kHasMaxConnection_ && available_connection_num_ != nullptr) {
    available_connection_num_->fetch_add(1, std::memory_order_relaxed);
  }
}

bool StreamServerConnection::IsServerRunning() const noexcept {
  return srv_->IsRunning();
}

bsrvcore::OwnedPtr<HttpRequestParser>&
StreamServerConnection::GetParser() noexcept {
  return parser_;
}

std::size_t StreamServerConnection::GetKeepAliveTimeout() const noexcept {
  return ((keep_alive_timeout_ / 1000) != 0u) ? keep_alive_timeout_ / 1000 : 1;
}

void StreamServerConnection::ArmTimeout(std::size_t timeout) {
  if (timeout == 0) {
    return;
  }

  timer_.expires_after(std::chrono::milliseconds(timeout));
  timer_.async_wait(
      [self = shared_from_this(), this](boost::system::error_code ec) {
        if (!ec) {
          DoClose();
        }
      });
}

void StreamServerConnection::CancelTimeout() { timer_.cancel(); }

IoExecutor StreamServerConnection::GetExecutor() const noexcept {
  return io_executor_;
}

FlatBuffer& StreamServerConnection::GetBuffer() { return buf_; }

void StreamServerConnection::ReserveReadBuffer(std::size_t bytes) {
  if (bytes == 0u) {
    return;
  }

  buf_.reserve(bytes);
}

void StreamServerConnection::ClearReadBuffer() {
  if (buf_.size() != 0u) {
    buf_.consume(buf_.size());
  }
}

void StreamServerConnection::ReserveRequestBodyBuffer(std::size_t bytes) {
  if (bytes == 0u || parser_ == nullptr) {
    return;
  }

  if (bytes >
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    bytes = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
  }

  parser_->get().body().reserve(bytes);
}

bsrvcore::HttpServer* StreamServerConnection::GetServer() const noexcept {
  return srv_;
}
