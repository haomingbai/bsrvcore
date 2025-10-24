/**
 * @file http_server.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/http_server.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/detail/chrono.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/context.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_route_result.h"
#include "bsrvcore/internal/empty_logger.h"
#include "bsrvcore/internal/http_route_table.h"
#include "bsrvcore/internal/http_server_connection_impl.h"
#include "bsrvcore/internal/session_map.h"
#include "bsrvcore/logger.h"

using namespace bsrvcore;

void HttpServer::SetTimer(std::size_t timeout, std::function<void()> fn) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (!is_running_) {
    return;
  }

  auto timer =
      std::make_shared<boost::asio::steady_timer>(thread_pool_->get_executor());
  timer->expires_after(boost::asio::chrono::milliseconds(timeout));
  timer->async_wait([fn, timer](boost::system::error_code ec) {
    if (!ec) {
      fn();
    }
  });
}

void HttpServer::Post(std::function<void()> fn) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (!is_running_) {
    return;
  }

  boost::asio::post(thread_pool_->get_executor(), fn);
}

void HttpServer::Log(LogLevel level, std::string message) {
  logger_->Log(level, std::move(message));
}

HttpRouteResult HttpServer::Route(HttpRequestMethod method,
                                  std::string_view target) {
  return route_table_->Route(method, target);
}

std::shared_ptr<Context> HttpServer::GetSession(const std::string& sessionid) {
  return sessions_->GetSession(sessionid);
}

std::shared_ptr<Context> HttpServer::GetSession(std::string&& sessionid) {
  return sessions_->GetSession(std::move(sessionid));
}

bool HttpServer::SetSessionTimeout(const std::string& sessionid,
                                   std::size_t timeout) {
  sessions_->SetSessionTimeout(sessionid, timeout);
  return true;
}

bool HttpServer::SetSessionTimeout(std::string&& sessionid,
                                   std::size_t timeout) {
  sessions_->SetSessionTimeout(sessionid, timeout);
  return true;
}

std::shared_ptr<Context> HttpServer::GetContext() { return context_; }

std::size_t HttpServer::GetKeepAliveTimeout() { return keep_alive_timeout_; }

bool HttpServer::IsRunning() { return is_running_; }

