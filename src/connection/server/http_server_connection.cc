/**
 * @file http_server_connection.cc
 * @brief HttpServerConnection base implementation.
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

#include "bsrvcore/internal/connection/server/http_server_connection.h"

#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/beast.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/string_body.hpp>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/session/context.h"

using bsrvcore::HttpServerConnection;

void HttpServerConnection::Post(std::function<void()> fn) {
  if (!srv_) {
    return;
  }

  srv_->Post(boost::asio::bind_allocator(GetHandlerAllocator(),
                                         [fn = std::move(fn)]() { fn(); }));
}

void HttpServerConnection::Dispatch(std::function<void()> fn) {
  if (!srv_) {
    return;
  }

  srv_->Dispatch(boost::asio::bind_allocator(GetHandlerAllocator(),
                                             [fn = std::move(fn)]() { fn(); }));
}

void HttpServerConnection::PostToIoContext(std::function<void()> fn) {
  if (!srv_) {
    return;
  }

  srv_->PostToIoContext(
      boost::asio::bind_allocator(GetHandlerAllocator(),
                                  [fn = std::move(fn)]() { fn(); }));
}

void HttpServerConnection::DispatchToIoContext(std::function<void()> fn) {
  if (!srv_) {
    return;
  }

  srv_->DispatchToIoContext(
      boost::asio::bind_allocator(GetHandlerAllocator(),
                                  [fn = std::move(fn)]() { fn(); }));
}

void HttpServerConnection::SetTimer(std::size_t timeout,
                                    std::function<void()> callback) {
  if (!srv_) {
    return;
  }

  srv_->SetTimer(
      timeout, boost::asio::bind_allocator(
                   GetHandlerAllocator(),
                   [callback = std::move(callback)]() mutable { callback(); }));
}

std::shared_ptr<bsrvcore::Context> HttpServerConnection::GetContext() noexcept {
  return srv_->GetContext();
}

std::shared_ptr<bsrvcore::Context> HttpServerConnection::GetSession(
    const std::string& sessionid) {
  return srv_->GetSession(sessionid);
}

std::shared_ptr<bsrvcore::Context> HttpServerConnection::GetSession(
    std::string&& sessionid) {
  return srv_->GetSession(std::move(sessionid));
}

bool HttpServerConnection::SetSessionTimeout(const std::string& sessionid,
                                             std::size_t timeout) {
  return srv_->SetSessionTimeout(sessionid, timeout);
}

bool HttpServerConnection::SetSessionTimeout(std::string&& sessionid,
                                             std::size_t timeout) {
  return srv_->SetSessionTimeout(std::move(sessionid), timeout);
}

void HttpServerConnection::Log(bsrvcore::LogLevel level, std::string message) {
  srv_->Log(level, std::move(message));
}

void HttpServerConnection::Run() {
  if (!IsServerRunning() || !IsStreamAvailable()) {
    DoClose();
    return;
  }

  boost::asio::dispatch(
      strand_, boost::asio::bind_allocator(
                   GetHandlerAllocator(), [self = shared_from_this(), this] {
                     if (header_read_expiry_) {
                       timer_.expires_after(
                           std::chrono::milliseconds(header_read_expiry_));
                       timer_.async_wait(boost::asio::bind_allocator(
                           GetHandlerAllocator(),
                           [self, this](boost::system::error_code ec) {
                             if (!ec) {
                               DoClose();
                             }
                           }));
                     }
                     DoReadHeader();
                   }));
}

void HttpServerConnection::DoRoute() {
  timer_.cancel();

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

  route_result_ = srv_->Route(
      HttpServer::BeastHttpVerbToHttpRequestMethod(res.method()), target);

  if (route_result_.handler == nullptr) {
    DoClose();
    return;
  }

  if (route_result_.read_expiry) {
    timer_.expires_after(std::chrono::milliseconds(route_result_.read_expiry));
    timer_.async_wait(boost::asio::bind_allocator(
        GetHandlerAllocator(),
        [self = shared_from_this(), this](boost::system::error_code ec) {
          if (!ec) {
            DoClose();
          }
        }));
  }

  if (route_result_.max_body_size) {
    parser_->body_limit(route_result_.max_body_size);
  }

  DoReadBody();
}

void HttpServerConnection::DoForwardRequest() {
  if (!IsServerRunning() || !IsStreamAvailable()) {
    DoClose();
    return;
  }

  timer_.cancel();
  std::shared_ptr<HttpPreServerTask> task = HttpPreServerTask::Create(
      parser_->release(), std::move(route_result_), shared_from_this());
  task->Start();
}

void HttpServerConnection::DoCycle() {
  ClearMessage();
  if (IsServerRunning() && IsStreamAvailable()) {
    route_result_ = {};
    parser_ = AllocateUnique<
        boost::beast::http::request_parser<boost::beast::http::string_body>>();
    if (header_read_expiry_ + keep_alive_timeout_) {
      timer_.expires_after(
          std::chrono::milliseconds(header_read_expiry_ + keep_alive_timeout_));
      timer_.async_wait(boost::asio::bind_allocator(
          GetHandlerAllocator(),
          [self = shared_from_this(), this](boost::system::error_code ec) {
            if (!ec) {
              DoClose();
            }
          }));
    }

    Run();
  } else {
    DoClose();
    return;
  }
}

HttpServerConnection::HttpServerConnection(
    boost::asio::strand<boost::asio::any_io_executor> strand, HttpServer* srv,
    std::size_t header_read_expiry, std::size_t keep_alive_timeout,
    bool has_max_connection,
    std::atomic<std::int64_t>* available_connection_num)
    : strand_(std::move(strand)),
      timer_(strand_),
      buf_(4096),
      srv_(srv),
      parser_(AllocateUnique<boost::beast::http::request_parser<
                  boost::beast::http::string_body>>()),
      header_read_expiry_(header_read_expiry),
      keep_alive_timeout_(keep_alive_timeout),
      kHasMaxConnection_(has_max_connection),
      available_connection_num_(available_connection_num),
      handler_alloc_() {
  if (kHasMaxConnection_ && available_connection_num_ != nullptr) {
    available_connection_num_->fetch_sub(1, std::memory_order_relaxed);
  }
}

HttpServerConnection::~HttpServerConnection() {
  if (kHasMaxConnection_ && available_connection_num_ != nullptr) {
    available_connection_num_->fetch_add(1, std::memory_order_relaxed);
  }
}

bool HttpServerConnection::IsServerRunning() const noexcept {
  return srv_->IsRunning();
}

bsrvcore::OwnedPtr<
    boost::beast::http::request_parser<boost::beast::http::string_body>>&
HttpServerConnection::GetParser() noexcept {
  return parser_;
}

std::size_t HttpServerConnection::GetKeepAliveTimeout() const noexcept {
  return keep_alive_timeout_ / 1000 ? keep_alive_timeout_ / 1000 : 1;
}

boost::asio::strand<boost::asio::any_io_executor>&
HttpServerConnection::GetStrand() {
  return strand_;
}

boost::asio::strand<boost::asio::any_io_executor>
HttpServerConnection::GetExecutor() {
  return strand_;
}

boost::beast::flat_buffer& HttpServerConnection::GetBuffer() { return buf_; }

bsrvcore::HttpServer* HttpServerConnection::GetServer() const noexcept {
  return srv_;
}
