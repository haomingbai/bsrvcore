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

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cctype>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/connection/server/server_set_cookie.h"
#include "bsrvcore/internal/connection/server/http_server_connection.h"

namespace bsrvcore {

namespace task_internal {

struct HttpTaskSharedState {
  HttpTaskSharedState(HttpRequest in_req, HttpRouteResult in_route_result,
                      std::shared_ptr<HttpServerConnection> in_conn,
                      bsrvcore::internal::HandlerAllocator in_handler_alloc)
      : req(std::move(in_req)),
        resp(),
        conn(std::move(in_conn)),
        route_result(std::move(in_route_result)),
        srv(conn.load() ? conn.load()->GetServer() : nullptr),
        keep_alive(true),
        manual_connection_management(false),
        is_cookie_parsed(false),
        handler_alloc(std::move(in_handler_alloc)) {}

  HttpRequest req;
  HttpResponse resp;
  std::unordered_map<std::string, std::string> cookies;
  std::optional<std::string> sessionid;
  std::vector<ServerSetCookie> set_cookies;
  std::atomic<std::shared_ptr<HttpServerConnection>> conn;
  HttpRouteResult route_result;
  HttpServer* srv;
  std::atomic_bool keep_alive;
  std::atomic_bool manual_connection_management;
  bool is_cookie_parsed;
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
  if (!ptr) {
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
    HttpRequest req, HttpRouteResult route_result,
    std::shared_ptr<HttpServerConnection> conn) {
  auto handler_alloc = conn ? conn->GetHandlerAllocator()
                            : bsrvcore::internal::HandlerAllocator{};
  bsrvcore::Allocator<HttpTaskSharedState> state_alloc;
  return std::allocate_shared<HttpTaskSharedState>(
      state_alloc, std::move(req), std::move(route_result), std::move(conn),
      handler_alloc);
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
  for (unsigned char c : sv) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

inline std::vector<std::string_view> SplitCookieHeaderUsingSplit(
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
                  return TrimView(std::string_view{it, len});
                });

  return {tokens.begin(), tokens.end()};
}

inline std::string GenerateSessionId() noexcept {
  static thread_local boost::uuids::random_generator tls_uuid_gen{};
  boost::uuids::uuid uuid = tls_uuid_gen();
  return boost::uuids::to_string(uuid);
}

inline std::shared_ptr<HttpServerConnection> GetConnection(
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
  state->conn.store(nullptr);
}

inline void AppendSetCookies(HttpTaskSharedState& state) {
  for (const auto& it : state.set_cookies) {
    auto set_cookie_string = it.ToString();
    if (!set_cookie_string.empty()) {
      state.resp.insert(boost::beast::http::field::set_cookie,
                        set_cookie_string);
    }
  }
}

inline void FinalizeResponse(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  if (!state || state->manual_connection_management.load()) {
    return;
  }

  auto conn = GetConnection(state);
  if (!conn || !conn->IsStreamAvailable()) {
    TryCloseConnection(state);
    return;
  }

  AppendSetCookies(*state);
  conn->DoWriteResponse(std::move(state->resp), state->keep_alive.load(),
                        state->route_result.write_expiry);
  state->conn.store(nullptr);
}

}  // namespace connection_internal::helper

}  // namespace bsrvcore

#endif
