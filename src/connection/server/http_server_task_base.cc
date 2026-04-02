/**
 * @file http_server_task_base.cc
 * @brief Shared HttpTaskBase implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/system/error_code.hpp>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/connection/server/server_set_cookie.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/internal/connection/server/http_server_task_detail.h"
#include "bsrvcore/session/context.h"

namespace bsrvcore {

namespace {

using connection_internal::helper::CanScheduleOnServer;
using connection_internal::helper::GetConnection;

inline bool CanRunTaskCallback(
    const std::shared_ptr<task_internal::HttpTaskSharedState>& state) {
  return CanScheduleOnServer(state);
}

/**
 * @brief Execute the second hop of a task timer on the worker pool.
 *
 * @details
 * Timer waiting happens on the raw io_context, but task callbacks are expected
 * to re-enter the server worker executor. Naming this second hop keeps that
 * thread transition visible near SetTimer().
 */
inline void PostTaskTimerCallback(
    const std::shared_ptr<task_internal::HttpTaskSharedState>& state,
    std::function<void()> fn) {
  boost::asio::post(
      state->srv->GetExecutor(),
      boost::asio::bind_allocator(state->handler_alloc,
                                  [fn = std::move(fn)]() mutable { fn(); }));
}

/**
 * @brief Handle timer completion after the io_context wait finishes.
 *
 * @details
 * The io_context owns the timer object, so this callback only validates that
 * the request task is still alive and then posts a second named hop back to
 * the worker executor.
 */
inline void OnTaskTimerExpired(
    const std::shared_ptr<task_internal::HttpTaskSharedState>& state,
    const std::shared_ptr<boost::asio::steady_timer>& timer,
    std::function<void()> fn, boost::system::error_code ec) {
  (void)timer;
  if (ec || !CanRunTaskCallback(state)) {
    return;
  }

  PostTaskTimerCallback(state, std::move(fn));
}

}  // namespace

HttpTaskBase::HttpTaskBase(
    std::shared_ptr<task_internal::HttpTaskSharedState> state)
    : state_(std::move(state)) {}

task_internal::HttpTaskSharedState& HttpTaskBase::GetState() noexcept {
  return *state_;
}

const task_internal::HttpTaskSharedState& HttpTaskBase::GetState()
    const noexcept {
  return *state_;
}

std::shared_ptr<task_internal::HttpTaskSharedState>
HttpTaskBase::GetSharedState() const noexcept {
  return state_;
}

HttpRequest& HttpTaskBase::GetRequest() noexcept { return state_->req; }

HttpResponse& HttpTaskBase::GetResponse() noexcept { return state_->resp; }

void HttpTaskBase::GenerateCookiePairs() {
  if (state_->is_cookie_parsed) {
    return;
  }

  // Cookies are parsed lazily so requests that never inspect cookie state do
  // not pay the split/trim cost.
  auto cookie_raw = state_->req[boost::beast::http::field::cookie];
  using connection_internal::helper::ParseCookiePairView;
  using connection_internal::helper::SplitCookieHeaderUsingSplit;
  for (const auto token : SplitCookieHeaderUsingSplit(cookie_raw)) {
    auto cookie_pair = ParseCookiePairView(token);
    if (!cookie_pair.first.empty()) {
      state_->cookies[std::string(cookie_pair.first)] = cookie_pair.second;
    }
  }

  state_->is_cookie_parsed = true;
}

const std::string& HttpTaskBase::GetCookie(const std::string& key) {
  GenerateCookiePairs();
  return state_->cookies[key];
}

const std::string& HttpTaskBase::GetSessionId() {
  GenerateCookiePairs();

  for (const auto& it : state_->cookies) {
    using connection_internal::helper::ToLowerString;
    // RFC cookie names are case-sensitive in theory, but real deployments tend
    // to vary this header spelling. Normalize here so session recovery is
    // tolerant to common client/proxy behavior.
    if (ToLowerString(it.first) == "sessionid") {
      state_->sessionid = it.second;
      break;
    }
  }

  if (!state_->sessionid.has_value()) {
    using connection_internal::helper::GenerateSessionId;
    state_->sessionid = GenerateSessionId();

    if (state_->sessionid.has_value()) {
      // Session creation is implicit on first access. Emitting Set-Cookie here
      // keeps the rest of the session API free from "maybe create" branches.
      ServerSetCookie session_cookie;
      session_cookie.SetName("sessionId")
          .SetValue(state_->sessionid.value_or(""));
      AddCookie(session_cookie);
    }
  }

  return state_->sessionid.value();
}

std::shared_ptr<Context> HttpTaskBase::GetSession() {
  auto conn = GetConnection(state_);
  return conn ? conn->GetSession(GetSessionId()) : nullptr;
}

std::shared_ptr<Context> HttpTaskBase::GetContext() noexcept {
  auto conn = GetConnection(state_);
  return conn ? conn->GetContext() : nullptr;
}

bool HttpTaskBase::SetSessionTimeout(std::size_t timeout) {
  auto conn = GetConnection(state_);
  return conn ? conn->SetSessionTimeout(GetSessionId(), timeout) : false;
}

void HttpTaskBase::SetBody(std::string body) {
  state_->resp.body() = std::move(body);
}

void HttpTaskBase::AppendBody(const std::string_view body) {
  state_->resp.body() += body;
}

void HttpTaskBase::SetField(const std::string_view key,
                            const std::string_view value) {
  state_->resp.set(key, value);
}

void HttpTaskBase::SetField(boost::beast::http::field key,
                            const std::string_view value) {
  state_->resp.set(key, value);
}

void HttpTaskBase::SetKeepAlive(bool value) noexcept {
  state_->keep_alive.store(value);
}

void HttpTaskBase::SetManualConnectionManagement(bool value) noexcept {
  if (!state_->manual_connection_management.load()) {
    // This flag is intentionally one-way. Once user code opts into manual
    // response control, later helpers must not silently re-enable auto write.
    state_->manual_connection_management.store(value);
  }
}

void HttpTaskBase::Log(bsrvcore::LogLevel level, const std::string& message) {
  if (state_->srv != nullptr) {
    state_->srv->Log(level, message);
  }
}

void HttpTaskBase::WriteBody(std::string body) {
  auto conn = GetConnection(state_);
  if (!conn) {
    return;
  }

  // Manual mode can stream headers/body in multiple chunks; the lifecycle
  // finalizer skips automatic DoWriteResponse() once this mode is enabled.
  conn->DoFlushResponseBody(std::move(body), state_->route_result.write_expiry);
}

void HttpTaskBase::WriteHeader(bsrvcore::HttpResponseHeader header) {
  auto conn = GetConnection(state_);
  if (!conn) {
    return;
  }

  conn->DoFlushResponseHeader(std::move(header),
                              state_->route_result.write_expiry);
}

void HttpTaskBase::Post(std::function<void()> fn) {
  if (!CanRunTaskCallback(state_)) {
    return;
  }

  boost::asio::post(
      state_->srv->GetExecutor(),
      boost::asio::bind_allocator(state_->handler_alloc,
                                  [fn = std::move(fn)]() mutable { fn(); }));
}

void HttpTaskBase::Dispatch(std::function<void()> fn) {
  if (!CanRunTaskCallback(state_)) {
    return;
  }

  boost::asio::dispatch(
      state_->srv->GetExecutor(),
      boost::asio::bind_allocator(state_->handler_alloc,
                                  [fn = std::move(fn)]() mutable { fn(); }));
}

void HttpTaskBase::PostToIoContext(std::function<void()> fn) {
  if (!CanRunTaskCallback(state_)) {
    return;
  }

  auto conn = GetConnection(state_);
  if (!conn) {
    return;
  }

  // Use the connection-local IO executor when follow-up work must stay ordered
  // with socket operations instead of hopping onto the general worker pool.
  boost::asio::post(
      conn->GetIoExecutor(),
      boost::asio::bind_allocator(state_->handler_alloc,
                                  [fn = std::move(fn)]() mutable { fn(); }));
}

void HttpTaskBase::DispatchToIoContext(std::function<void()> fn) {
  if (!CanRunTaskCallback(state_)) {
    return;
  }

  auto conn = GetConnection(state_);
  if (!conn) {
    return;
  }

  boost::asio::dispatch(
      conn->GetIoExecutor(),
      boost::asio::bind_allocator(state_->handler_alloc,
                                  [fn = std::move(fn)]() mutable { fn(); }));
}

void HttpTaskBase::SetTimer(std::size_t timeout, std::function<void()> fn) {
  if (!CanRunTaskCallback(state_)) {
    return;
  }

  auto conn = GetConnection(state_);
  if (!conn) {
    return;
  }

  auto timer = AllocateShared<boost::asio::steady_timer>(conn->GetIoExecutor());
  timer->expires_after(std::chrono::milliseconds(timeout));
  timer->async_wait(boost::asio::bind_allocator(
      state_->handler_alloc,
      [state = state_, timer = std::move(timer),
       fn = std::move(fn)](boost::system::error_code ec) mutable {
        // Timers are owned by the IO executor so cancellation/shutdown is
        // aligned with connection lifetime, then the callback hops back to the
        // worker pool via OnTaskTimerExpired().
        OnTaskTimerExpired(std::move(state), std::move(timer), std::move(fn),
                           ec);
      }));
}

bool HttpTaskBase::IsAvailable() noexcept { return CanRunTaskCallback(state_); }

const std::string& HttpTaskBase::GetCurrentLocation() {
  return state_->route_result.current_location;
}

const std::string& HttpTaskBase::GetRouteTemplate() {
  return state_->route_result.route_template;
}

const std::unordered_map<std::string, std::string>&
HttpTaskBase::GetPathParameters() {
  return state_->route_result.parameters;
}

const std::string* HttpTaskBase::GetPathParameter(const std::string& key) {
  const auto it = state_->route_result.parameters.find(key);
  return it == state_->route_result.parameters.end() ? nullptr : &it->second;
}

bool HttpTaskBase::AddCookie(const bsrvcore::ServerSetCookie& cookie) try {
  state_->set_cookies.emplace_back(cookie);
  return true;
} catch (...) {
  return false;
}

void HttpTaskBase::DoClose() {
  auto conn = GetConnection(state_);
  if (!conn) {
    return;
  }

  conn->DoClose();
  state_->conn.store(nullptr);
}

void HttpTaskBase::DoCycle() {
  auto conn = GetConnection(state_);
  if (!conn) {
    return;
  }

  conn->DoCycle();
  state_->conn.store(nullptr);
}

boost::asio::any_io_executor HttpTaskBase::GetIoExecutor() noexcept {
  auto conn = GetConnection(state_);
  if (!conn) {
    return {};
  }

  return conn->GetIoExecutor();
}

boost::asio::any_io_executor HttpTaskBase::GetExecutor() noexcept {
  if (!state_ || state_->srv == nullptr) {
    return {};
  }
  return state_->srv->GetExecutor();
}

std::vector<boost::asio::any_io_executor>
HttpTaskBase::GetEndpointExecutors() noexcept {
  auto conn = GetConnection(state_);
  return conn ? conn->GetEndpointExecutors()
              : std::vector<boost::asio::any_io_executor>{};
}

std::vector<boost::asio::any_io_executor>
HttpTaskBase::GetGlobalExecutors() noexcept {
  auto conn = GetConnection(state_);
  return conn ? conn->GetGlobalExecutors()
              : std::vector<boost::asio::any_io_executor>{};
}

}  // namespace bsrvcore
