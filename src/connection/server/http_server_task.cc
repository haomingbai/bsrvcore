/**
 * @file http_server_task.cc
 * @brief HTTP server lifecycle task implementations.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-06
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements HttpTaskBase and request phase tasks (pre/service/post),
 * including cookie/session handling, posting helpers, and timers.
 */

#include "bsrvcore/connection/server/http_server_task.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/server_set_cookie.h"
#include "bsrvcore/core/logger.h"
#include "bsrvcore/internal/connection/server/http_server_connection.h"
#include "bsrvcore/session/context.h"

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

  // Per-request allocator + executor, derived from the owning
  // connection/server.
  bsrvcore::internal::HandlerAllocator handler_alloc;
};

}  // namespace task_internal

namespace connection_internal {
namespace helper {

using task_internal::HttpTaskSharedState;

inline void DestroyTaskObject(const std::shared_ptr<HttpTaskSharedState>& state,
                              void* ptr, std::size_t size,
                              std::size_t alignment) {
  (void)state;
  if (!ptr) return;
  bsrvcore::Deallocate(ptr, size, alignment);
}

template <typename T>
inline T* AllocateTaskObject(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  (void)state;
  return static_cast<T*>(bsrvcore::Allocate(sizeof(T), alignof(T)));
}

inline std::string_view TrimView(std::string_view sv) {
  constexpr std::string_view ws = " \t\r\n";
  auto first = sv.find_first_not_of(ws);

  if (first == std::string_view::npos) {
    return std::string_view{};
  }

  auto last = sv.find_last_not_of(ws);
  return sv.substr(first, last - first + 1);
}

inline std::pair<std::string_view, std::string_view> ParseCookiePairView(
    std::string_view token) {
  token = TrimView(token);
  if (token.empty()) {
    return {std::string_view{}, std::string_view{}};
  }

  auto eq = token.find('=');

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

inline std::string ToLowerString(const std::string_view sv) {
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
                    return std::string_view{};
                  }
                  auto len =
                      static_cast<std::size_t>(ranges::distance(it, it_end));
                  std::string_view sv{it, len};
                  return TrimView(sv);
                });

  std::vector<std::string_view> out(tokens.begin(), tokens.end());
  return out;
}

inline std::string GenerateSessionId() noexcept {
  static thread_local boost::uuids::random_generator tls_uuid_gen{};
  boost::uuids::uuid uuid = tls_uuid_gen();
  return boost::uuids::to_string(uuid);
}

inline std::shared_ptr<HttpServerConnection> GetConnection(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  if (!state) {
    return nullptr;
  }
  return state->conn.load();
}

inline bool IsTaskEnvironmentAvailable(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  if (!state || state->srv == nullptr || !state->srv->IsRunning()) {
    return false;
  }

  auto conn = GetConnection(state);
  return conn && conn->IsStreamAvailable();
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

inline void FinalizeResponse(
    const std::shared_ptr<HttpTaskSharedState>& state) {
  if (!state) {
    return;
  }

  if (state->manual_connection_management.load()) {
    return;
  }

  auto conn = GetConnection(state);
  if (!conn || !conn->IsStreamAvailable()) {
    TryCloseConnection(state);
    return;
  }

  for (const auto& it : state->set_cookies) {
    auto set_cookie_string = it.ToString();
    if (!set_cookie_string.empty()) {
      state->resp.insert(boost::beast::http::field::set_cookie,
                         set_cookie_string);
    }
  }

  conn->DoWriteResponse(std::move(state->resp), state->keep_alive.load(),
                        state->route_result.write_expiry);
  state->conn.store(nullptr);
}

}  // namespace helper
}  // namespace connection_internal

namespace task_internal {

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

void HttpPreTaskDeleter::operator()(HttpPreServerTask* ptr) const {
  if (ptr) {
    ptr->~HttpPreServerTask();
    connection_internal::helper::DestroyTaskObject(
        state, ptr, sizeof(HttpPreServerTask), alignof(HttpPreServerTask));
  }

  if (state->route_result.handler == nullptr ||
      !connection_internal::helper::IsTaskEnvironmentAvailable(state)) {
    connection_internal::helper::TryCloseConnection(state);
    return;
  }

  auto* raw =
      connection_internal::helper::AllocateTaskObject<HttpServerTask>(state);
  new (raw) HttpServerTask(state);
  auto task = std::shared_ptr<HttpServerTask>(raw, HttpServerTaskDeleter{state},
                                              state->handler_alloc);
  task->Start();
}

void HttpServerTaskDeleter::operator()(HttpServerTask* ptr) const {
  if (ptr) {
    ptr->~HttpServerTask();
    connection_internal::helper::DestroyTaskObject(
        state, ptr, sizeof(HttpServerTask), alignof(HttpServerTask));
  }

  if (!state) {
    return;
  }

  if (!connection_internal::helper::IsTaskEnvironmentAvailable(state)) {
    connection_internal::helper::TryCloseConnection(state);
    return;
  }

  if (state->route_result.aspects.empty()) {
    connection_internal::helper::FinalizeResponse(state);
    return;
  }

  auto* raw =
      connection_internal::helper::AllocateTaskObject<HttpPostServerTask>(
          state);
  new (raw) HttpPostServerTask(state);
  auto task = std::shared_ptr<HttpPostServerTask>(
      raw, HttpPostTaskDeleter{state}, state->handler_alloc);
  task->Start();
}

void HttpPostTaskDeleter::operator()(HttpPostServerTask* ptr) const {
  if (ptr) {
    ptr->~HttpPostServerTask();
    connection_internal::helper::DestroyTaskObject(
        state, ptr, sizeof(HttpPostServerTask), alignof(HttpPostServerTask));
  }

  if (!state) {
    return;
  }

  if (!connection_internal::helper::IsTaskEnvironmentAvailable(state)) {
    connection_internal::helper::TryCloseConnection(state);
    return;
  }

  connection_internal::helper::FinalizeResponse(state);
}

}  // namespace task_internal

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

  auto cookie_raw = state_->req[boost::beast::http::field::cookie];
  using bsrvcore::connection_internal::helper::ParseCookiePairView;
  using bsrvcore::connection_internal::helper::SplitCookieHeaderUsingSplit;
  auto cookie_strs = SplitCookieHeaderUsingSplit(cookie_raw);
  for (const auto it : cookie_strs) {
    auto cookie_pair = ParseCookiePairView(it);
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
    using bsrvcore::connection_internal::helper::ToLowerString;
    if (ToLowerString(it.first) == "sessionid") {
      state_->sessionid = it.second;
      break;
    }
  }

  if (!state_->sessionid.has_value()) {
    using bsrvcore::connection_internal::helper::GenerateSessionId;
    state_->sessionid = GenerateSessionId();

    if (state_->sessionid.has_value()) {
      ServerSetCookie session_cookie;
      session_cookie.SetName("sessionId")
          .SetValue(state_->sessionid.value_or(""));
      AddCookie(std::move(session_cookie));
    }
  }

  return state_->sessionid.value();
}

std::shared_ptr<Context> HttpTaskBase::GetSession() {
  auto conn = state_->conn.load();

  if (!conn) {
    return nullptr;
  }

  return conn->GetSession(GetSessionId());
}

std::shared_ptr<Context> HttpTaskBase::GetContext() noexcept {
  auto conn = state_->conn.load();

  if (!conn) {
    return nullptr;
  }

  return conn->GetContext();
}

bool HttpTaskBase::SetSessionTimeout(std::size_t timeout) {
  auto conn = state_->conn.load();

  if (!conn) {
    return false;
  }

  return conn->SetSessionTimeout(GetSessionId(), timeout);
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
  state_->keep_alive.store(value ? true : false);
}

void HttpTaskBase::SetManualConnectionManagement(bool value) noexcept {
  if (!state_->manual_connection_management.load()) {
    state_->manual_connection_management.store(value ? true : false);
  }
}

void HttpTaskBase::Log(bsrvcore::LogLevel level, const std::string message) {
  if (state_->srv) {
    state_->srv->Log(level, std::move(message));
  }
}

void HttpTaskBase::WriteBody(std::string body) {
  auto conn = state_->conn.load();

  if (!conn) {
    return;
  }

  conn->DoFlushResponseBody(std::move(body), state_->route_result.write_expiry);
}

void HttpTaskBase::WriteHeader(bsrvcore::HttpResponseHeader header) {
  auto conn = state_->conn.load();

  if (!conn) {
    return;
  }

  conn->DoFlushResponseHeader(std::move(header),
                              state_->route_result.write_expiry);
}

void HttpTaskBase::Post(std::function<void()> fn) {
  if (!state_ || state_->srv == nullptr || !state_->srv->IsRunning()) {
    return;
  }

  auto conn = state_->conn.load();
  if (!conn) {
    return;
  }

  boost::asio::post(
      state_->srv->GetExecutor(),
      boost::asio::bind_allocator(state_->handler_alloc,
                                  [fn = std::move(fn)]() mutable { fn(); }));
}

void HttpTaskBase::Dispatch(std::function<void()> fn) {
  if (!state_ || state_->srv == nullptr || !state_->srv->IsRunning()) {
    return;
  }

  auto conn = state_->conn.load();
  if (!conn) {
    return;
  }

  boost::asio::dispatch(
      state_->srv->GetExecutor(),
      boost::asio::bind_allocator(state_->handler_alloc,
                                  [fn = std::move(fn)]() mutable { fn(); }));
}

void HttpTaskBase::PostToIoContext(std::function<void()> fn) {
  if (!state_ || state_->srv == nullptr || !state_->srv->IsRunning()) {
    return;
  }

  auto conn = state_->conn.load();
  if (!conn) {
    return;
  }

  boost::asio::post(
      state_->srv->GetIoContext(),
      boost::asio::bind_allocator(state_->handler_alloc,
                                  [fn = std::move(fn)]() mutable { fn(); }));
}

void HttpTaskBase::DispatchToIoContext(std::function<void()> fn) {
  if (!state_ || state_->srv == nullptr || !state_->srv->IsRunning()) {
    return;
  }

  auto conn = state_->conn.load();
  if (!conn) {
    return;
  }

  boost::asio::dispatch(
      state_->srv->GetIoContext(),
      boost::asio::bind_allocator(state_->handler_alloc,
                                  [fn = std::move(fn)]() mutable { fn(); }));
}

void HttpTaskBase::SetTimer(std::size_t timeout, std::function<void()> fn) {
  if (!state_ || state_->srv == nullptr || !state_->srv->IsRunning()) {
    return;
  }

  auto conn = state_->conn.load();
  if (!conn) {
    return;
  }

  auto timer =
      AllocateShared<boost::asio::steady_timer>(state_->srv->GetIoContext());
  timer->expires_after(boost::asio::chrono::milliseconds(timeout));
  timer->async_wait(boost::asio::bind_allocator(
      state_->handler_alloc, [state = state_, timer, fn = std::move(fn)](
                                 boost::system::error_code ec) mutable {
        if (ec || !state || state->srv == nullptr || !state->srv->IsRunning()) {
          return;
        }

        boost::asio::post(state->srv->GetExecutor(),
                          boost::asio::bind_allocator(
                              state->handler_alloc,
                              [fn = std::move(fn)]() mutable { fn(); }));
      }));
}

bool HttpTaskBase::IsAvailable() noexcept {
  auto conn = state_->conn.load();
  return conn && state_->srv != nullptr && state_->srv->IsRunning() &&
         conn->IsStreamAvailable();
}

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
  auto it = state_->route_result.parameters.find(key);
  if (it == state_->route_result.parameters.end()) {
    return nullptr;
  }
  return &it->second;
}

bool HttpTaskBase::AddCookie(bsrvcore::ServerSetCookie cookie) try {
  state_->set_cookies.emplace_back(std::move(cookie));
  return true;
} catch (...) {
  return false;
}

void HttpTaskBase::DoClose() {
  auto conn = state_->conn.load();

  if (!conn) {
    return;
  }

  conn->DoClose();
  state_->conn.store(nullptr);
}

void HttpTaskBase::DoCycle() {
  auto conn = state_->conn.load();

  if (!conn) {
    return;
  }

  conn->DoCycle();
  state_->conn.store(nullptr);
}

std::shared_ptr<HttpPreServerTask> HttpPreServerTask::Create(
    HttpRequest req, HttpRouteResult route_result,
    std::shared_ptr<HttpServerConnection> conn) {
  auto handler_alloc = conn ? conn->GetHandlerAllocator()
                            : bsrvcore::internal::HandlerAllocator{};
  bsrvcore::Allocator<task_internal::HttpTaskSharedState> state_alloc;
  auto state = std::allocate_shared<task_internal::HttpTaskSharedState>(
      state_alloc, std::move(req), std::move(route_result), std::move(conn),
      handler_alloc);

  auto* raw =
      connection_internal::helper::AllocateTaskObject<HttpPreServerTask>(state);
  new (raw) HttpPreServerTask(state);
  return std::shared_ptr<HttpPreServerTask>(
      raw, task_internal::HttpPreTaskDeleter{state}, state->handler_alloc);
}

HttpPreServerTask::HttpPreServerTask(
    std::shared_ptr<task_internal::HttpTaskSharedState> state)
    : HttpTaskBase(std::move(state)) {}

HttpPreServerTask::~HttpPreServerTask() = default;

void HttpPreServerTask::Start() {
  auto conn = GetSharedState()->conn.load();
  if (!conn) {
    return;
  }

  auto self = shared_from_this();
  conn->DispatchToConnectionExecutor(boost::asio::bind_allocator(
      GetState().handler_alloc,
      [self = std::move(self)]() mutable { self->DoPreService(0); }));
}

void HttpPreServerTask::DoPreService(std::size_t curr_idx) {
  auto& state = GetState();
  if (curr_idx >= state.route_result.aspects.size()) {
    return;
  }

  auto self = shared_from_this();
  while (curr_idx < state.route_result.aspects.size()) {
    state.route_result.aspects[curr_idx]->PreService(self);
    ++curr_idx;
  }
}

HttpServerTask::HttpServerTask(HttpRequest req, HttpRouteResult route_result,
                               std::shared_ptr<HttpServerConnection> conn)
    : HttpTaskBase([&] {
        auto handler_alloc = conn ? conn->GetHandlerAllocator()
                                  : bsrvcore::internal::HandlerAllocator{};
        bsrvcore::Allocator<task_internal::HttpTaskSharedState> state_alloc;
        return std::allocate_shared<task_internal::HttpTaskSharedState>(
            state_alloc, std::move(req), std::move(route_result),
            std::move(conn), handler_alloc);
      }()) {}

HttpServerTask::HttpServerTask(
    std::shared_ptr<task_internal::HttpTaskSharedState> state)
    : HttpTaskBase(std::move(state)) {}

HttpServerTask::~HttpServerTask() = default;

void HttpServerTask::Start() {
  auto conn = GetSharedState()->conn.load();
  if (!conn) {
    return;
  }

  auto self = shared_from_this();
  conn->DispatchToConnectionExecutor(boost::asio::bind_allocator(
      GetState().handler_alloc,
      [self = std::move(self)]() mutable { self->DoService(); }));
}

void HttpServerTask::DoService() {
  auto& state = GetState();
  if (state.route_result.handler == nullptr) {
    return;
  }

  auto self = shared_from_this();
  state.route_result.handler->Service(self);
}

std::shared_ptr<HttpServerTask> HttpServerTask::Create(
    HttpRequest req, HttpRouteResult route_result,
    std::shared_ptr<HttpServerConnection> conn) {
  auto handler_alloc = conn ? conn->GetHandlerAllocator()
                            : bsrvcore::internal::HandlerAllocator{};
  bsrvcore::Allocator<task_internal::HttpTaskSharedState> state_alloc;
  auto state = std::allocate_shared<task_internal::HttpTaskSharedState>(
      state_alloc, std::move(req), std::move(route_result), std::move(conn),
      handler_alloc);

  auto* raw =
      connection_internal::helper::AllocateTaskObject<HttpServerTask>(state);
  new (raw) HttpServerTask(state);
  return std::shared_ptr<HttpServerTask>(
      raw,
      [state](HttpServerTask* ptr) {
        if (ptr) {
          ptr->~HttpServerTask();
          connection_internal::helper::DestroyTaskObject(
              state, ptr, sizeof(HttpServerTask), alignof(HttpServerTask));
        }

        if (!state) {
          return;
        }

        if (!connection_internal::helper::IsTaskEnvironmentAvailable(state)) {
          connection_internal::helper::TryCloseConnection(state);
          return;
        }

        connection_internal::helper::FinalizeResponse(state);
      },
      state->handler_alloc);
}

HttpPostServerTask::HttpPostServerTask(
    std::shared_ptr<task_internal::HttpTaskSharedState> state)
    : HttpTaskBase(std::move(state)) {}

HttpPostServerTask::~HttpPostServerTask() = default;

void HttpPostServerTask::Start() {
  auto& state = GetState();
  if (state.route_result.aspects.empty()) {
    return;
  }

  auto conn = GetSharedState()->conn.load();
  if (!conn) {
    return;
  }

  auto self = shared_from_this();
  conn->DispatchToConnectionExecutor(boost::asio::bind_allocator(
      state.handler_alloc,
      [self = std::move(self),
       last_idx = state.route_result.aspects.size() - 1]() mutable {
        self->DoPostService(last_idx);
      }));
}

void HttpPostServerTask::DoPostService(std::size_t curr_idx) {
  auto& state = GetState();
  if (curr_idx >= state.route_result.aspects.size()) {
    assert(0);
    return;
  }

  auto self = shared_from_this();
  while (true) {
    state.route_result.aspects[curr_idx]->PostService(self);

    if (curr_idx == 0) {
      return;
    }

    --curr_idx;
  }
}

boost::asio::io_context& HttpTaskBase::GetIoContext() noexcept {
  return state_->conn.load()->GetServer()->GetIoContext();
}

boost::asio::any_io_executor HttpTaskBase::GetExecutor() noexcept {
  if (!state_ || state_->srv == nullptr) {
    return {};
  }

  return state_->srv->GetExecutor();
}

}  // namespace bsrvcore
