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

#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/internal/route/http_route_table.h"
#include "bsrvcore/internal/session/session_map.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/route/http_route_result.h"
#include "bsrvcore/session/context.h"

using namespace bsrvcore;

IoExecutor HttpServer::SelectIoExecutorRoundRobin() noexcept {
  // Start()/Stop() publish executor snapshots atomically, so hot-path reads can
  // stay lock-free and simply fall back to the control io_context while the
  // runtime is still coming up or already tearing down.
  auto global_snapshot =
      global_io_execs_snapshot_.load(std::memory_order_acquire);
  if (!global_snapshot || global_snapshot->empty()) {
    return control_ioc_.get_executor();
  }

  // Per-request IO work does not need strong ordering across threads; relaxed
  // round-robin is enough to spread short tasks over the available shards.
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

  auto timer = AllocateShared<SteadyTimer>(io_exec);
  timer->expires_after(std::chrono::milliseconds(timeout));
  timer->async_wait(
      [this, fn = std::move(fn), timer](boost::system::error_code ec) mutable {
        if (ec) {
          return;
        }

        if (!is_running_.load(std::memory_order_acquire)) {
          return;
        }

        // Keep timer bookkeeping on an IO shard, but run user callbacks on the
        // worker pool so timer-driven work follows the same execution model as
        // normal handlers and HttpServerTask helpers.
        Post(std::move(fn));
      });
}

void HttpServer::Post(std::function<void()> fn) {
  std::scoped_lock const lock(mtx_);
  if (!is_running_) {
    return;
  }

  // The mutex closes the race with Stop(), which tears the worker pool down
  // under the same lock.
  boost::asio::post(GetThreadPoolExecutor(), std::move(fn));
}

void HttpServer::Dispatch(std::function<void()> fn) {
  std::scoped_lock const lock(mtx_);
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
  // Routing stays centralized inside HttpRouteTable so server/runtime code does
  // not need to understand tree structure, aspect collection order, or route
  // policy inheritance.
  return route_table_->Route(method, target);
}

std::shared_ptr<Context> HttpServer::GetSession(const std::string& sessionid) {
  return sessions_->GetSession(sessionid);
}

std::shared_ptr<Context> HttpServer::GetSession(std::string&& sessionid) {
  return sessions_->GetSession(std::move(sessionid));
}

IoExecutor HttpServer::GetIoExecutor() noexcept {
  if (!is_running_.load(std::memory_order_acquire)) {
    return {};
  }

  // Public callers only need "an IO executor" for short follow-up work, not a
  // stable shard identity, so reuse the same round-robin selection strategy.
  return SelectIoExecutorRoundRobin();
}

IoExecutor HttpServer::GetExecutor() noexcept {
  std::scoped_lock const lock(mtx_);
  if (!is_running_) {
    return {};
  }

  return GetThreadPoolExecutor();
}

std::vector<IoExecutor> HttpServer::GetEndpointExecutors(
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

std::vector<IoExecutor> HttpServer::GetGlobalExecutors() noexcept {
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

std::size_t HttpServer::GetKeepAliveTimeout() const {
  return keep_alive_timeout_;
}

bool HttpServer::IsRunning() {
  return is_running_.load(std::memory_order_acquire);
}
