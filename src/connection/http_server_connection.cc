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
      timer_.async_wait(
          [self, this](boost::system::error_code ec) { DoClose(); });
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
  }

  if (route_result_.read_expiry) {
    timer_.expires_after(std::chrono::milliseconds(route_result_.read_expiry));
    timer_.async_wait([self = shared_from_this(),
                       this](boost::system::error_code ec) { DoClose(); });
  }

  if (route_result_.max_body_size) {
    parser_->body_limit(route_result_.max_body_size);
  }

  DoReadBody();
}

void HttpServerConnection::DoPreService(std::shared_ptr<HttpServerTask> task,
                                        std::size_t curr_idx) {
  if (task == nullptr) {
    DoClose();
    return;
  }

  if (!IsServerRunning() || !IsStreamAvailable()) {
    DoClose();
    return;
  }

  if (curr_idx > route_result_.aspects.size()) {
    assert(false);
    DoClose();
    return;
  }

  if (curr_idx == route_result_.aspects.size()) {
    srv_->Post(
        [task, self = shared_from_this(), this] { DoForwardRequest(task); });
  } else {
    srv_->Post([task, self = shared_from_this(), this, curr_idx] {
      route_result_.aspects[curr_idx]->PreService(task);
      DoPreService(task, curr_idx + 1);
    });
  }
}

void HttpServerConnection::DoPostService(std::shared_ptr<HttpServerTask> task,
                                         std::size_t curr_idx) {
  if (task == nullptr) {
    DoClose();
    return;
  }

  if (!IsServerRunning() || !IsStreamAvailable()) {
    DoClose();
    return;
  }

  if (curr_idx >= route_result_.aspects.size()) {
    assert(false);
    DoClose();
    return;
  }

  if (curr_idx == 0) {
    srv_->Post([task, self = shared_from_this(), this, curr_idx] {
      route_result_.aspects[curr_idx]->PostService(task);
    });
  } else {
    srv_->Post([task, self = shared_from_this(), this, curr_idx] {
      route_result_.aspects[curr_idx]->PostService(task);
      DoPostService(task, curr_idx - 1);
    });
  }
}

void HttpServerConnection::DoForwardRequest(
    std::shared_ptr<HttpServerTask> task) {
  if (task == nullptr) {
    DoClose();
    return;
  }

  if (!IsServerRunning() || !IsStreamAvailable()) {
    DoClose();
    return;
  }

  srv_->Post([task, self = shared_from_this(), this] {
    route_result_.handler->Service(task);
    if (!route_result_.aspects.empty()) {
      DoPostService(task, route_result_.aspects.size() - 1);
    }
  });
}

void HttpServerConnection::DoCycle() {
  if (IsServerRunning() && IsStreamAvailable()) {
    route_result_ = {};
    parser_ = std::make_unique<
        boost::beast::http::request_parser<boost::beast::http::string_body>>();
    if (header_read_expiry_ + keep_alive_timeout_) {
      timer_.expires_after(
          std::chrono::milliseconds(header_read_expiry_ + keep_alive_timeout_));
      timer_.async_wait([self = shared_from_this(),
                         this](boost::system::error_code ec) { DoClose(); });
    }

    Run();
  } else {
    DoClose();
    return;
  }
}

HttpServerConnection::HttpServerConnection(
    boost::asio::strand<boost::asio::any_io_executor> strand,
    std::shared_ptr<HttpServer> srv, std::size_t header_read_expiry,
    std::size_t keep_alive_timeout)
    : strand_(std::move(strand)),
      timer_(strand_),
      buf_(4096),
      parser_(std::make_unique<boost::beast::http::request_parser<
                  boost::beast::http::string_body>>()),
      header_read_expiry_(header_read_expiry),
      keep_alive_timeout_(keep_alive_timeout) {}

void HttpServerConnection::MakeHttpServerTask() {
  std::shared_ptr<HttpServerTask> task = std::make_shared<HttpServerTask>(
      parser_->release(), std::move(route_result_.parameters),
      std::move(route_result_.current_location), shared_from_this());
  DoPreService(task, 0);
}

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
