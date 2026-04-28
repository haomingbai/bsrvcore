/**
 * @file http_server_task_detail.h
 * @brief Internal helpers for HTTP server task lifecycle implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_CONNECTION_SERVER_HTTP_SERVER_TASK_DETAIL_H_
#define BSRVCORE_INTERNAL_CONNECTION_SERVER_HTTP_SERVER_TASK_DETAIL_H_

#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cctype>
#include <cstddef>
#include <cstdint>  // NOLINT(misc-include-cleaner): Boost.Beast field.hpp requires std::uint32_t on some toolchains.
#include <memory>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/connection/server/server_set_cookie.h"
#include "bsrvcore/connection/websocket/websocket_task_base.h"
#include "bsrvcore/core/types.h"
#include "bsrvcore/internal/connection/server/stream_server_connection.h"
#include "bsrvcore/internal/route/http_route_result_internal.h"

namespace bsrvcore {

namespace task_internal {

enum class WebSocketUpgradeState {
  kNone,
  kRequested,
  kHeaderPrepared,
  kTaskStarted,
};

// Shared request state survives the pre -> service -> post phase chain. The
// custom deleters in http_server_task_lifecycle.cc advance the lifecycle by
// constructing the next task object on top of this same state.
struct HttpTaskSharedState {
  using PerfCookieMap = AllocatedStdStringMap<std::string>;
  using PerfSetCookieList = AllocatedVector<ServerSetCookie>;
  using CompatPathParameterMap = std::unordered_map<std::string, std::string>;

  HttpTaskSharedState(HttpRequest in_req,
                      route_internal::HttpRouteResultInternal in_route_result,
                      std::shared_ptr<StreamServerConnection> in_conn,
                      bsrvcore::internal::HandlerAllocator in_handler_alloc)
      : req(std::move(in_req)),

        conn(std::move(in_conn)),
        route_result(std::move(in_route_result)),
        route_result_compat_current_location(
            detail::ToStdString(route_result.current_location)),
        route_result_compat_template(
            detail::ToStdString(route_result.route_template)),
        route_result_compat_parameters([this] {
          CompatPathParameterMap m;
          m.reserve(route_result.parameters.size());
          for (const auto& [k, v] : route_result.parameters)
            m.emplace(detail::ToStdString(k), detail::ToStdString(v));
          return m;
        }()),
        srv(conn.load() ? conn.load()->GetServer() : nullptr),
        keep_alive(true),
        connection_mode(HttpTaskConnectionLifecycleMode::kAutomatic),

        handler_alloc(in_handler_alloc) {}

  HttpRequest req;
  HttpResponse resp;
  PerfCookieMap cookies;
  std::optional<std::string> sessionid;
  PerfSetCookieList set_cookies;
  // The connection pointer is cleared when the response is finalized or the
  // stream is closed. Subsequent task callbacks treat nullptr as "request is no
  // longer schedulable".
  AtomicSharedPtr<StreamServerConnection> conn;
  route_internal::HttpRouteResultInternal route_result;
  std::string route_result_compat_current_location;
  std::string route_result_compat_template;
  CompatPathParameterMap route_result_compat_parameters;
  HttpServer* srv;
  std::atomic_bool keep_alive;
  std::atomic<HttpTaskConnectionLifecycleMode> connection_mode;
  std::atomic<WebSocketUpgradeState> websocket_upgrade_state{
      WebSocketUpgradeState::kNone};
  mutable std::shared_mutex websocket_upgrade_handler_mtx;
  WebSocketTaskBase::HandlerPtr websocket_upgrade_handler;
  bool is_cookie_parsed{false};
  bsrvcore::internal::HandlerAllocator handler_alloc;
};

struct HttpPreTaskDeleter {
  std::shared_ptr<HttpTaskSharedState> state;
  void operator()(HttpPreServerTask* ptr) const;
};

struct HttpServerTaskDeleter {
  std::shared_ptr<HttpTaskSharedState> state;
  void operator()(HttpServerTask* ptr) const;
};

struct HttpPostTaskDeleter {
  std::shared_ptr<HttpTaskSharedState> state;
  void operator()(HttpPostServerTask* ptr) const;
};

}  // namespace task_internal

namespace connection_internal::helper {

using task_internal::HttpTaskSharedState;

inline void DestroyTaskObject(const std::shared_ptr<HttpTaskSharedState>& state,
                              void* ptr, std::size_t size,
                              std::size_t alignment) {
  (void)state;
  if (ptr == nullptr) {
    return;
  }
  bsrvcore::Deallocate(ptr, size, alignment);
}

template <typename T>
inline T* AllocateTaskObject(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  (void)state;
  return static_cast<T*>(bsrvcore::Allocate(sizeof(T), alignof(T)));
}

inline std::shared_ptr<HttpTaskSharedState> CreateTaskState(
    HttpRequest req, route_internal::HttpRouteResultInternal route_result,
    std::shared_ptr<StreamServerConnection> conn) {
  // Reuse the connection's small-object allocator so all lifecycle handlers and
  // task objects stay consistent with the connection's Asio allocation policy.
  auto handler_alloc = conn ? conn->GetHandlerAllocator()
                            : bsrvcore::internal::HandlerAllocator{};
  bsrvcore::Allocator<HttpTaskSharedState> const state_alloc;
  return std::allocate_shared<HttpTaskSharedState>(
      state_alloc, std::move(req), std::move(route_result), std::move(conn),
      handler_alloc);
}

inline std::shared_ptr<HttpTaskSharedState> CreateTaskState(
    HttpRequest req, HttpRouteResult route_result,
    std::shared_ptr<StreamServerConnection> conn) {
  return CreateTaskState(
      std::move(req),
      route_internal::ToInternalRouteResult(std::move(route_result)),
      std::move(conn));
}

inline std::string_view TrimView(std::string_view sv) {
  constexpr std::string_view ws = " \t\r\n";
  const auto first = sv.find_first_not_of(ws);
  if (first == std::string_view::npos) {
    return {};
  }

  const auto last = sv.find_last_not_of(ws);
  return sv.substr(first, last - first + 1);
}

inline std::pair<std::string_view, std::string_view> ParseCookiePairView(
    std::string_view token) {
  token = TrimView(token);
  if (token.empty()) {
    return {std::string_view{}, std::string_view{}};
  }

  const auto eq = token.find('=');
  if (eq == std::string_view::npos) {
    return {TrimView(token), std::string_view{}};
  }

  auto name = TrimView(token.substr(0, eq));
  auto value = TrimView(token.substr(eq + 1));
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }

  return {name, value};
}

inline std::string ToLowerString(std::string_view sv) {
  std::string out;
  out.reserve(sv.size());
  for (unsigned char const c : sv) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

inline AllocatedVector<std::string_view> SplitCookieHeaderUsingSplit(
    std::string_view header) {
  namespace ranges = std::ranges;
  namespace views = std::views;

  auto tokens = header | views::split(';') |
                views::transform([](auto subrange) -> std::string_view {
                  auto it = ranges::begin(subrange);
                  auto it_end = ranges::end(subrange);
                  if (it == it_end) {
                    return {};
                  }
                  const auto len =
                      static_cast<std::size_t>(ranges::distance(it, it_end));
                  return TrimView(std::string_view{&*it, len});
                });

  return {tokens.begin(), tokens.end()};
}

inline std::string GenerateSessionId() noexcept {
  static thread_local boost::uuids::random_generator tls_uuid_gen{};
  boost::uuids::uuid const uuid = tls_uuid_gen();
  return boost::uuids::to_string(uuid);
}

inline std::shared_ptr<StreamServerConnection> GetConnection(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  return state ? state->conn.load() : nullptr;
}

inline bool IsTaskEnvironmentAvailable(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  if (!state || state->srv == nullptr || !state->srv->IsRunning()) {
    return false;
  }

  auto conn = GetConnection(state);
  return conn && conn->IsStreamAvailable();
}

inline bool CanScheduleOnServer(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  return IsTaskEnvironmentAvailable(state);
}

inline void TryCloseConnection(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  auto conn = GetConnection(state);
  if (!conn) {
    return;
  }

  if (conn->IsStreamAvailable()) {
    conn->DoClose();
  }
  // Clearing conn is just as important as closing the stream: delayed callbacks
  // use this as the guard that the request lifecycle has ended.
  state->conn.store(nullptr);
}

inline void AppendSetCookies(HttpTaskSharedState& state) {
  for (const auto& it : state.set_cookies) {
    auto set_cookie_string = it.ToString();
    if (!set_cookie_string.empty()) {
      state.resp.insert(HttpField::set_cookie, set_cookie_string);
    }
  }
}

inline HttpResponseHeader MakeResponseHeaderSnapshot(const HttpResponse& resp) {
  HttpResponseHeader header;
  header.result(resp.result());
  header.version(resp.version());
  header.reason(resp.reason());

  for (const auto& field : resp) {
    header.set(field.name_string(), field.value());
  }

  return header;
}

enum class ResponseWriteStageResult {
  kWriteSubmitted,
  kWebSocketAcceptPending,
  kSkippedManualManagement,
  kConnectionUnavailable,
};

inline ResponseWriteStageResult SubmitFinalResponseWriteIfNeeded(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  if (!state) {
    return ResponseWriteStageResult::kConnectionUnavailable;
  }

  const auto mode = state->connection_mode.load(std::memory_order_acquire);
  if (mode == HttpTaskConnectionLifecycleMode::kManual) {
    return ResponseWriteStageResult::kSkippedManualManagement;
  }

  auto conn = GetConnection(state);
  if (!conn || !conn->IsStreamAvailable()) {
    TryCloseConnection(state);
    return ResponseWriteStageResult::kConnectionUnavailable;
  }

  if (mode == HttpTaskConnectionLifecycleMode::kWebSocket) {
    AppendSetCookies(*state);
    state->resp.body().clear();
    state->websocket_upgrade_state.store(
        bsrvcore::task_internal::WebSocketUpgradeState::kHeaderPrepared,
        std::memory_order_release);
    return ResponseWriteStageResult::kWebSocketAcceptPending;
  }

  AppendSetCookies(*state);
  // Response writing and connection keep-alive/close decisions are delegated
  // to StreamServerConnection::DoWriteResponse().
  conn->DoWriteResponse(std::move(state->resp), state->keep_alive.load(),
                        state->route_result.write_expiry);
  state->conn.store(nullptr);
  return ResponseWriteStageResult::kWriteSubmitted;
}

inline void FinalizeManualConnectionAction(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  if (!state) {
    return;
  }

  auto conn = GetConnection(state);
  if (!conn || !conn->IsStreamAvailable()) {
    TryCloseConnection(state);
    return;
  }

  if (state->keep_alive.load()) {
    conn->DoCycle();
  } else {
    conn->DoClose();
  }

  state->conn.store(nullptr);
}

}  // namespace connection_internal::helper

}  // namespace bsrvcore

#endif
