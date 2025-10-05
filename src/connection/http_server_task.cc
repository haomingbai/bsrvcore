/**
 * @file http_server_task.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-02
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/http_server_task.h"

#include <boost/beast/http/field.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/context.h"
#include "bsrvcore/internal/http_server_connection.h"
#include "bsrvcore/logger.h"
#include "bsrvcore/server_set_cookie.h"

using bsrvcore::HttpRequest;
using bsrvcore::HttpResponse;
using bsrvcore::HttpServerTask;

namespace bsrvcore {
namespace connection_internal {
namespace helper {

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
    // If there is only name and have no value
    return {TrimView(token), std::string_view{}};
  }

  auto name = TrimView(token.substr(0, eq));
  auto value = TrimView(token.substr(eq + 1));

  // Remove the embrace
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }

  return {name, value};
}

inline std::string ToLowerString(const std::string_view sv) {
  std::string out;
  out.reserve(sv.size());
  for (unsigned char c : sv) out.push_back(static_cast<char>(std::tolower(c)));
  return out;
}

inline std::vector<std::string_view> SplitCookieHeaderUsingSplit(
    std::string_view header) {
  namespace views = std::views;
  namespace ranges = std::ranges;

  // Split with C++ 20
  auto tokens = header | views::split(';') |
                views::transform([](auto subrange) -> std::string_view {
                  auto it = ranges::begin(subrange);
                  auto it_end = ranges::end(subrange);
                  if (it == it_end) return std::string_view{};
                  //  Transform it to string_view
                  auto len =
                      static_cast<std::size_t>(ranges::distance(it, it_end));
                  std::string_view sv{it, len};
                  return TrimView(sv);
                });

  std::vector<std::string_view> out(tokens.begin(), tokens.end());

  return out;
}

inline std::string GenerateSessionId() noexcept {
  // thread-local generator -> no global locking, good for concurrency
  static thread_local boost::uuids::random_generator tls_uuid_gen{};
  boost::uuids::uuid u = tls_uuid_gen();
  return boost::uuids::to_string(u);
}

}  // namespace helper
}  // namespace connection_internal
}  // namespace bsrvcore

HttpRequest &HttpServerTask::GetRequest() noexcept { return req_; }

HttpResponse &HttpServerTask::GetResponse() noexcept { return resp_; }

HttpServerTask::HttpServerTask(HttpRequest req, HttpRouteResult route_result,
                               std::shared_ptr<HttpServerConnection> conn)
    : req_(std::move(req)),
      resp_(),
      conn_(std::move(conn)),
      route_result_(std::move(route_result)),
      srv_(conn_.load()->GetServer()),
      keep_alive_(true),
      manual_connection_management_(false),  // Initialize the new flag
      is_cookie_parsed_(false) {}

void HttpServerTask::GenerateCookiePairs() {
  if (is_cookie_parsed_) {
    return;
  }

  auto cookie_raw = req_[boost::beast::http::field::cookie];
  using bsrvcore::connection_internal::helper::SplitCookieHeaderUsingSplit;
  auto cookie_strs = SplitCookieHeaderUsingSplit(cookie_raw);
  for (const auto it : cookie_strs) {
    using bsrvcore::connection_internal::helper::ParseCookiePairView;
    auto cookie_pair = ParseCookiePairView(it);
    if (!cookie_pair.first.empty()) {
      cookies_[std::string(cookie_pair.first)] = cookie_pair.second;
    }
  }
}

const std::string &HttpServerTask::GetCookie(const std::string &key) {
  GenerateCookiePairs();

  return cookies_[key];
}

const std::string &HttpServerTask::GetSessionId() {
  GenerateCookiePairs();

  for (const auto &it : cookies_) {
    using bsrvcore::connection_internal::helper::ToLowerString;
    if (ToLowerString(it.first) == "sessionid") {
      sessionid_ = it.second;
      break;
    }
  }

  if (!sessionid_.has_value()) {
    using bsrvcore::connection_internal::helper::GenerateSessionId;
    sessionid_ = GenerateSessionId();

    if (sessionid_.has_value()) {
      ServerSetCookie session_cookie;
      session_cookie.SetName("sessionId").SetValue(sessionid_.value_or(""));
      AddCookie(std::move(session_cookie));
    }
  }

  return sessionid_.value();
}

std::shared_ptr<bsrvcore::Context> HttpServerTask::GetSession() {
  auto conn = conn_.load();

  if (!conn) {
    return nullptr;
  }
  return conn->GetSession(GetSessionId());
}

std::shared_ptr<bsrvcore::Context> HttpServerTask::GetContext() noexcept {
  auto conn = conn_.load();

  if (!conn) {
    return nullptr;
  }

  return conn->GetContext();
}

bool HttpServerTask::SetSessionTimeout(std::size_t timeout) {
  auto conn = conn_.load();

  if (!conn) {
    return false;
  }

  return conn->SetSessionTimeout(GetSessionId(), timeout);
}

void HttpServerTask::SetBody(std::string body) {
  resp_.body() = std::move(body);
}

void HttpServerTask::AppendBody(const std::string_view body) {
  resp_.body() += body;
}

void HttpServerTask::SetField(const std::string_view key,
                              const std::string_view value) {
  resp_.set(key, value);
}

void HttpServerTask::SetField(boost::beast::http::field key,
                              const std::string_view value) {
  resp_.set(key, value);
}

void HttpServerTask::SetKeepAlive(bool value) noexcept {
  keep_alive_ = value ? true : false;
}

void HttpServerTask::SetManualConnectionManagement(bool value) noexcept {
  if (!manual_connection_management_) {
    manual_connection_management_ = value ? true : false;
  }
}

void HttpServerTask::Log(bsrvcore::LogLevel level, const std::string message) {
  srv_->Log(level, std::move(message));
}

void HttpServerTask::WriteBody(std::string body) {
  auto conn = conn_.load();

  if (!conn) {
    return;
  }
  conn->DoFlushResponseBody(std::move(body));
}

void HttpServerTask::WriteHeader(bsrvcore::HttpResponseHeader header) {
  auto conn = conn_.load();

  if (!conn) {
    return;
  }
  conn->DoFlushResponseHeader(std::move(header));
}

void HttpServerTask::Post(std::function<void()> fn) {
  if (srv_->IsRunning()) {
    srv_->Post(fn);
  }
}

void HttpServerTask::SetTimer(std::size_t timeout, std::function<void()> fn) {
  srv_->SetTimer(timeout, fn);
}

bool HttpServerTask::IsAvailable() noexcept {
  auto conn = conn_.load();
  return conn && srv_->IsRunning() && conn->IsStreamAvailable();
}

const std::string &HttpServerTask::GetCurrentLocation() {
  return route_result_.current_location;
}

const std::vector<std::string> &HttpServerTask::GetPathParameters() {
  return route_result_.parameters;
}

HttpServerTask::~HttpServerTask() {
  // If connection lifetime is managed manually (e.g., for SSE),
  // the destructor should do nothing to the connection.
  if (manual_connection_management_) {
    return;
  }

  auto conn = conn_.load();

  if (!conn) {
    return;
  }

  for (const auto &it : set_cookies_) {
    auto set_cookie_string = it.ToString();
    if (!set_cookie_string.empty()) {
      resp_.insert(boost::beast::http::field::set_cookie, set_cookie_string);
    }
  }

  conn->DoWriteResponse(std::move(resp_), keep_alive_);
}

bool HttpServerTask::AddCookie(bsrvcore::ServerSetCookie cookie) try {
  set_cookies_.emplace_back(std::move(cookie));
  return true;
} catch (...) {
  return false;
}

void HttpServerTask::DoClose() {
  auto conn = conn_.load();

  if (!conn) {
    return;
  }

  conn->DoClose();
  conn_.load().reset();
}

void HttpServerTask::DoCycle() {
  auto conn = conn_.load();

  if (!conn) {
    return;
  }

  conn->DoCycle();
  conn_.load().reset();
}

void HttpServerTask::Start() {
  Post([self = shared_from_this(), this] { DoPreService(0); });
}

void HttpServerTask::DoPreService(std::size_t curr_idx) {
  if (curr_idx > route_result_.aspects.size()) {
    assert(0);
    return;
  }

  if (curr_idx == route_result_.aspects.size()) {
    Post([self = shared_from_this(), this] { DoService(); });
  } else {
    route_result_.aspects[curr_idx]->PreService(shared_from_this());
    Post([self = shared_from_this(), this, curr_idx] {
      DoPreService(curr_idx + 1);
    });
  }
}

void HttpServerTask::DoService() {
  route_result_.handler->Service(shared_from_this());

  if (!route_result_.aspects.empty()) {
    Post([self = shared_from_this(), this] {
      DoPostService(route_result_.aspects.size() - 1);
    });
  }
}

void HttpServerTask::DoPostService(std::size_t curr_idx) {
  if (curr_idx >= route_result_.aspects.size()) {
    assert(0);
    return;
  }

  route_result_.aspects[curr_idx]->PostService(shared_from_this());

  if (curr_idx != 0) {
    Post([self = shared_from_this(), this, curr_idx] {
      DoPostService(curr_idx - 1);
    });
  }
}
