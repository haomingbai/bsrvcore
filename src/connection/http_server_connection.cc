/**
 * @file http_server_connection.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-30
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/internal/http_server_connection.h"

#include <boost/asio/post.hpp>
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

#include "bsrvcore/context.h"
#include "bsrvcore/http_request_aspect_handler.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "bsrvcore/logger.h"

using bsrvcore::HttpServerConnection;

void HttpServerConnection::Post(std::function<void()> fn) { srv_->Post(fn); }

void HttpServerConnection::SetTimer(std::size_t timeout,
                                    std::function<void()> callback) {
  srv_->SetTimer(timeout, callback);
}

std::shared_ptr<bsrvcore::Context> HttpServerConnection::GetContext() noexcept {
  return srv_->GetContext();
}

std::shared_ptr<bsrvcore::Context> HttpServerConnection::GetSession(
    const std::string &sessionid) {
  return srv_->GetSession(sessionid);
}

std::shared_ptr<bsrvcore::Context> HttpServerConnection::GetSession(
    std::string &&sessionid) {
  return srv_->GetSession(std::move(sessionid));
}

bool HttpServerConnection::SetSessionTimeout(const std::string &sessionid,
                                             std::size_t timeout) {
  return srv_->SetSessionTimeout(sessionid, timeout);
}

bool HttpServerConnection::SetSessionTimeout(std::string &&sessionid,
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

  boost::asio::post(strand_, [self = shared_from_this(), this] {
    if (header_read_expiry_) {
      timer_.expires_after(std::chrono::milliseconds(header_read_expiry_));
      timer_.async_wait([self, this](boost::system::error_code ec) {
        if (!ec) {
          DoClose();
        }
      });
    }
    DoReadHeader();
  });
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

  auto &res = parser_->get();
  auto target = res.target();

  route_result_ = srv_->Route(
      HttpServer::BeastHttpVerbToHttpRequestMethod(res.method()), target);

  if (route_result_.handler == nullptr) {
    DoClose();
    return;
  }

  if (route_result_.read_expiry) {
    timer_.expires_after(std::chrono::milliseconds(route_result_.read_expiry));
    timer_.async_wait(
        [self = shared_from_this(), this](boost::system::error_code ec) {
          if (!ec) {
            DoClose();
          }
        });
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
  std::shared_ptr<HttpServerTask> task = std::make_shared<HttpServerTask>(
      parser_->release(),
      std::make_unique<HttpRouteResult>(std::move(route_result_)),
      shared_from_this());
  task->Start();
}

void HttpServerConnection::DoCycle() {
  ClearMessage();
  if (IsServerRunning() && IsStreamAvailable()) {
    route_result_ = {};
    parser_ = std::make_unique<
        boost::beast::http::request_parser<boost::beast::http::string_body>>();
    if (header_read_expiry_ + keep_alive_timeout_) {
      timer_.expires_after(
          std::chrono::milliseconds(header_read_expiry_ + keep_alive_timeout_));
      timer_.async_wait(
          [self = shared_from_this(), this](boost::system::error_code ec) {
            if (!ec) {
              DoClose();
            }
          });
    }

    Run();
  } else {
    DoClose();
    return;
  }
}

HttpServerConnection::HttpServerConnection(
    boost::asio::strand<boost::asio::any_io_executor> strand, HttpServer *srv,
    std::size_t header_read_expiry, std::size_t keep_alive_timeout)
    : strand_(std::move(strand)),
      timer_(strand_),
      buf_(4096),
      srv_(srv),
      parser_(std::make_unique<boost::beast::http::request_parser<
                  boost::beast::http::string_body>>()),
      header_read_expiry_(header_read_expiry),
      keep_alive_timeout_(keep_alive_timeout) {}

bool HttpServerConnection::IsServerRunning() const noexcept {
  return srv_->IsRunning();
}

std::unique_ptr<
    boost::beast::http::request_parser<boost::beast::http::string_body>> &
HttpServerConnection::GetParser() noexcept {
  return parser_;
}

std::size_t HttpServerConnection::GetKeepAliveTimeout() const noexcept {
  return keep_alive_timeout_ / 1000 ? keep_alive_timeout_ / 1000 : 1;
}

boost::asio::strand<boost::asio::any_io_executor> &
HttpServerConnection::GetStrand() {
  return strand_;
}

boost::asio::strand<boost::asio::any_io_executor>
HttpServerConnection::GetExecutor() {
  return strand_;
}

boost::beast::flat_buffer &HttpServerConnection::GetBuffer() { return buf_; }

bsrvcore::HttpServer *HttpServerConnection::GetServer() const noexcept {
  return srv_;
}
