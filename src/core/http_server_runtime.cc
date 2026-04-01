/**
 * @file http_server_runtime.cc
 * @brief HttpServer runtime API implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements runtime methods such as timers, executor posting, logging,
 * routing and session access.
 */

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/detail/chrono.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/internal/route/http_route_table.h"
#include "bsrvcore/internal/session/session_map.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/route/http_route_result.h"
#include "bsrvcore/session/context.h"

using namespace bsrvcore;

boost::asio::any_io_executor HttpServer::SelectIoExecutorRoundRobin() noexcept {
  auto global_snapshot =
      global_io_execs_snapshot_.load(std::memory_order_acquire);
  if (!global_snapshot || global_snapshot->empty()) {
    return control_ioc_.get_executor();
  }

  const auto idx =
      io_exec_round_robin_.fetch_add(1, std::memory_order_relaxed) %
      global_snapshot->size();
  return (*global_snapshot)[idx];
}

void HttpServer::SetTimer(std::size_t timeout, std::function<void()> fn) {
  if (!is_running_.load(std::memory_order_acquire)) {
    return;
  }

  auto io_exec = SelectIoExecutorRoundRobin();
  if (!io_exec) {
    return;
  }

  auto timer = AllocateShared<boost::asio::steady_timer>(io_exec);
  timer->expires_after(boost::asio::chrono::milliseconds(timeout));
  timer->async_wait(
      [this, fn = std::move(fn), timer](boost::system::error_code ec) mutable {
        if (ec) {
          return;
        }

        if (!is_running_.load(std::memory_order_acquire)) {
          return;
        }

        Post(std::move(fn));
      });
}

void HttpServer::Post(std::function<void()> fn) {
  std::lock_guard<std::mutex> lock(mtx_);
  if (!is_running_) {
    return;
  }

  boost::asio::post(GetThreadPoolExecutor(), std::move(fn));
}

void HttpServer::Dispatch(std::function<void()> fn) {
  std::lock_guard<std::mutex> lock(mtx_);
  if (!is_running_) {
    return;
  }

  boost::asio::dispatch(GetThreadPoolExecutor(), std::move(fn));
}

void HttpServer::PostToIoContext(std::function<void()> fn) {
  if (!is_running_.load(std::memory_order_acquire)) {
    return;
  }

  auto exec = SelectIoExecutorRoundRobin();
  if (!exec) {
    return;
  }

  boost::asio::post(exec, std::move(fn));
}

void HttpServer::DispatchToIoContext(std::function<void()> fn) {
  if (!is_running_.load(std::memory_order_acquire)) {
    return;
  }

  auto exec = SelectIoExecutorRoundRobin();
  if (!exec) {
    return;
  }

  boost::asio::dispatch(exec, std::move(fn));
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

boost::asio::any_io_executor HttpServer::GetIoExecutor() noexcept {
  if (!is_running_.load(std::memory_order_acquire)) {
    return {};
  }

  return SelectIoExecutorRoundRobin();
}

boost::asio::any_io_executor HttpServer::GetExecutor() noexcept {
  std::lock_guard<std::mutex> lock(mtx_);
  if (!is_running_) {
    return {};
  }

  return GetThreadPoolExecutor();
}

std::vector<boost::asio::any_io_executor> HttpServer::GetEndpointExecutors(
    std::size_t endpoint_index) noexcept {
  if (!is_running_.load(std::memory_order_acquire)) {
    return {};
  }

  auto endpoint_snapshot =
      endpoint_io_execs_snapshot_.load(std::memory_order_acquire);
  if (!endpoint_snapshot || endpoint_index >= endpoint_snapshot->size()) {
    return {};
  }

  return (*endpoint_snapshot)[endpoint_index];
}

std::vector<boost::asio::any_io_executor>
HttpServer::GetGlobalExecutors() noexcept {
  if (!is_running_.load(std::memory_order_acquire)) {
    return {};
  }

  auto global_snapshot =
      global_io_execs_snapshot_.load(std::memory_order_acquire);
  if (!global_snapshot) {
    return {};
  }

  return *global_snapshot;
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

bool HttpServer::IsRunning() {
  return is_running_.load(std::memory_order_acquire);
}
