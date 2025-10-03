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

HttpServerTask::HttpServerTask(HttpRequest req, std::vector<std::string> params,
                               std::string current_location,
                               std::shared_ptr<HttpServerConnection> conn)
    : req_(std::move(req)),
      resp_(),
      parameters_(std::move(params)),
      current_location_(std::move(current_location)),
      conn_(std::move(conn)),
      keep_alive_(true),
      autowrite_(true),
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
  }

  return sessionid_.value();
}

std::shared_ptr<bsrvcore::Context> HttpServerTask::GetSession() {
  if (!conn_) {
    return nullptr;
  }
  return conn_->GetSession(GetSessionId());
}

std::shared_ptr<bsrvcore::Context> HttpServerTask::GetContext() noexcept {
  if (!conn_) {
    return nullptr;
  }

  return conn_->GetContext();
}

bool HttpServerTask::SetSessionTimeout(std::size_t timeout) {
  if (!conn_) {
    return false;
  }
  return conn_->SetSessionTimeout(GetSessionId(), timeout);
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

void HttpServerTask::SetAutowrite(bool value) noexcept {
  if (autowrite_) {
    autowrite_ = value ? true : false;
  }
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
  if (!conn_) {
    return;
  }
  conn_->Log(level, std::move(message));
}

void HttpServerTask::WriteBody(std::string body) {
  if (!conn_) {
    return;
  }
  conn_->DoFlushResponseBody(std::move(body));
}

void HttpServerTask::WriteHeader(bsrvcore::HttpResponseHeader header) {
  if (!conn_) {
    return;
  }
  conn_->DoFlushResponseHeader(std::move(header));
}

void HttpServerTask::Post(std::function<void()> fn) { conn_->Post(fn); }

void HttpServerTask::SetTimer(std::size_t timeout, std::function<void()> fn) {
  if (!conn_) {
    return;
  }
  conn_->SetTimer(timeout, fn);
}

bool HttpServerTask::IsAvailable() noexcept {
  return conn_ && conn_->IsServerRunning() && conn_->IsStreamAvailable();
}

const std::string &HttpServerTask::GetCurrentLocation() {
  return current_location_;
}

const std::vector<std::string> &HttpServerTask::GetPathParameters() {
  return parameters_;
}

HttpServerTask::~HttpServerTask() {
  // If connection lifetime is managed manually (e.g., for SSE),
  // the destructor should do nothing to the connection.
  if (manual_connection_management_) {
    return;
  }

  if (!conn_) {
    return;
  }

  if (autowrite_) {
    for (const auto &it : set_cookies_) {
      auto set_cookie_string = it.ToString();
      if (!set_cookie_string.empty()) {
        resp_.set(boost::beast::http::field::set_cookie,
                  std::move(set_cookie_string));
      }
    }

    conn_->DoWriteResponse(std::move(resp_), keep_alive_);
  } else {
    // This block is problematic for long-lived connections like SSE.
    // If autowrite is false, it means we are manually writing.
    // However, calling DoCycle() here immediately resets the connection
    // to read the *next* request, causing a race condition with
    // any pending manual writes. This is now prevented by the
    // `manual_connection_management_` flag.
    if (keep_alive_) {
      conn_->DoCycle();
    } else {
      conn_->DoClose();
    }
  }
}

bool HttpServerTask::AddCookie(bsrvcore::ServerSetCookie cookie) try {
  set_cookies_.emplace_back(std::move(cookie));
  return true;
} catch (...) {
  return false;
}

void HttpServerTask::DoClose() {
  if (!conn_) {
    return;
  }

  conn_->DoClose();
  conn_ = nullptr;
}
