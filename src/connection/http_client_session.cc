/**
 * @file http_client_session.cc
 * @brief HttpClientSession implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/http_client_session.h"

#include <algorithm>
#include <boost/beast/http/field.hpp>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/allocator.h"

namespace bsrvcore {

namespace {

inline std::string_view TrimView(std::string_view sv) {
  constexpr std::string_view ws = " \t\r\n";
  auto first = sv.find_first_not_of(ws);
  if (first == std::string_view::npos) {
    return std::string_view{};
  }
  auto last = sv.find_last_not_of(ws);
  return sv.substr(first, last - first + 1);
}

inline bool IEquals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    unsigned char ca = static_cast<unsigned char>(a[i]);
    unsigned char cb = static_cast<unsigned char>(b[i]);
    if (std::tolower(ca) != std::tolower(cb)) {
      return false;
    }
  }
  return true;
}

inline std::pair<std::string_view, std::string_view> SplitOnce(
    std::string_view s, char delim) {
  auto pos = s.find(delim);
  if (pos == std::string_view::npos) {
    return {s, std::string_view{}};
  }
  return {s.substr(0, pos), s.substr(pos + 1)};
}

inline std::optional<std::chrono::system_clock::time_point> ParseImfFixdate(
    std::string_view v) {
  // IMF-fixdate: "Sun, 06 Nov 1994 08:49:37 GMT"
  // For portability we parse only this common format; failures fall back to
  // session cookie semantics.
  std::tm tm{};
  std::istringstream iss{std::string(v)};
  iss.imbue(std::locale::classic());
  iss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
  if (iss.fail()) {
    return std::nullopt;
  }

  // Convert tm (treated as UTC) to time_t.
  // timegm is non-standard; emulate using std::chrono by adjusting from mktime
  // in UTC assumption is not portable. For our "medium" cookie support we keep
  // Expires best-effort: interpret via timegm when available, otherwise skip.
#if defined(_GNU_SOURCE) || defined(__GLIBC__)
  time_t t = timegm(&tm);
  if (t == static_cast<time_t>(-1)) {
    return std::nullopt;
  }
  return std::chrono::system_clock::from_time_t(t);
#else
  (void)tm;
  return std::nullopt;
#endif
}

}  // namespace

struct HttpClientSession::Cookie {
  std::string name;
  std::string value;

  // Domain scoping.
  std::string domain;  // normalized lower-case host/domain
  bool host_only{true};

  // Path scoping.
  std::string path{"/"};

  bool secure{false};
  std::optional<std::chrono::system_clock::time_point> expiry;
};

std::shared_ptr<HttpClientSession> HttpClientSession::Create() {
  void* raw = Allocate(sizeof(HttpClientSession), alignof(HttpClientSession));
  try {
    auto* session = new (raw) HttpClientSession();
    return std::shared_ptr<HttpClientSession>(
        session, [](HttpClientSession* ptr) { DestroyDeallocate(ptr); });
  } catch (...) {
    Deallocate(raw, sizeof(HttpClientSession), alignof(HttpClientSession));
    throw;
  }
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateHttp(
    boost::asio::any_io_executor executor, std::string host, std::string port,
    std::string target, boost::beast::http::verb method,
    HttpClientOptions options) {
  auto task = HttpClientTask::CreateHttp(std::move(executor), std::move(host),
                                         std::move(port), std::move(target),
                                         method, std::move(options));
  task->AttachSession(weak_from_this());
  return task;
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateHttps(
    boost::asio::any_io_executor executor, boost::asio::ssl::context& ssl_ctx,
    std::string host, std::string port, std::string target,
    boost::beast::http::verb method, HttpClientOptions options) {
  auto task = HttpClientTask::CreateHttps(
      std::move(executor), ssl_ctx, std::move(host), std::move(port),
      std::move(target), method, std::move(options));
  task->AttachSession(weak_from_this());
  return task;
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateFromUrl(
    boost::asio::any_io_executor executor, std::string url,
    boost::beast::http::verb method, HttpClientOptions options) {
  auto task = HttpClientTask::CreateFromUrl(std::move(executor), std::move(url),
                                            method, std::move(options));
  task->AttachSession(weak_from_this());
  return task;
}

std::shared_ptr<HttpClientTask> HttpClientSession::CreateFromUrl(
    boost::asio::any_io_executor executor, boost::asio::ssl::context& ssl_ctx,
    std::string url, boost::beast::http::verb method,
    HttpClientOptions options) {
  auto task = HttpClientTask::CreateFromUrl(
      std::move(executor), ssl_ctx, std::move(url), method, std::move(options));
  task->AttachSession(weak_from_this());
  return task;
}

void HttpClientSession::ClearCookies() {
  std::lock_guard<std::mutex> lock(mutex_);
  cookies_.clear();
}

std::size_t HttpClientSession::CookieCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  CleanupExpiredLocked();
  return cookies_.size();
}

void HttpClientSession::MaybeInjectCookies(HttpClientRequest& request,
                                           std::string_view host,
                                           std::string_view target,
                                           bool is_https) {
  namespace http = boost::beast::http;
  if (request.find(http::field::cookie) != request.end()) {
    // Respect user-provided Cookie header.
    return;
  }

  std::string cookie = [&]() {
    std::lock_guard<std::mutex> lock(mutex_);
    CleanupExpiredLocked();
    return BuildCookieHeaderLocked(host, target, is_https);
  }();

  if (!cookie.empty()) {
    request.set(http::field::cookie, cookie);
  }
}

void HttpClientSession::SyncSetCookie(std::string_view host,
                                      std::string_view target,
                                      std::string_view set_cookie_value) {
  std::lock_guard<std::mutex> lock(mutex_);
  CleanupExpiredLocked();
  UpsertFromSetCookieLocked(host, target, set_cookie_value);
}

void HttpClientSession::CleanupExpiredLocked() const {
  const auto now = std::chrono::system_clock::now();
  cookies_.erase(std::remove_if(cookies_.begin(), cookies_.end(),
                                [&](const Cookie& c) {
                                  return c.expiry.has_value() &&
                                         c.expiry.value() <= now;
                                }),
                 cookies_.end());
}

std::string HttpClientSession::NormalizeHost(std::string_view host) {
  std::string out;
  out.reserve(host.size());
  for (unsigned char c : host) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

std::string HttpClientSession::NormalizeDomain(std::string_view domain) {
  domain = TrimView(domain);
  if (!domain.empty() && domain.front() == '.') {
    domain.remove_prefix(1);
  }
  return NormalizeHost(domain);
}

std::string HttpClientSession::DefaultPathFromTarget(std::string_view target) {
  // Target could include query. Only the path part is used.
  auto q = target.find('?');
  std::string_view path =
      (q == std::string_view::npos) ? target : target.substr(0, q);
  if (path.empty() || path.front() != '/') {
    return "/";
  }
  auto last = path.find_last_of('/');
  if (last == std::string_view::npos || last == 0) {
    return "/";
  }
  return std::string(path.substr(0, last));
}

bool HttpClientSession::DomainMatches(std::string_view host,
                                      std::string_view domain) {
  if (host == domain) {
    return true;
  }
  if (host.size() <= domain.size()) {
    return false;
  }
  const std::size_t offset = host.size() - domain.size();
  if (host.compare(offset, domain.size(), domain) != 0) {
    return false;
  }
  // Ensure boundary: "x.example.com" matches "example.com" but
  // "badexample.com" does not.
  return host[offset - 1] == '.';
}

bool HttpClientSession::PathMatches(std::string_view request_path,
                                    std::string_view cookie_path) {
  if (cookie_path.empty() || cookie_path.front() != '/') {
    cookie_path = "/";
  }

  if (cookie_path == "/") {
    return true;
  }
  if (request_path.rfind(cookie_path, 0) != 0) {
    return false;
  }
  if (request_path.size() == cookie_path.size()) {
    return true;
  }
  if (!cookie_path.empty() && cookie_path.back() == '/') {
    return true;
  }
  return request_path[cookie_path.size()] == '/';
}

std::string HttpClientSession::BuildCookieHeaderLocked(std::string_view host,
                                                       std::string_view target,
                                                       bool is_https) const {
  const std::string host_norm = NormalizeHost(host);
  auto q = target.find('?');
  const std::string_view request_path =
      (q == std::string_view::npos) ? target : target.substr(0, q);

  struct Candidate {
    std::size_t idx;
    std::size_t path_len;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(cookies_.size());

  for (std::size_t i = 0; i < cookies_.size(); ++i) {
    const Cookie& c = cookies_[i];
    if (c.secure && !is_https) {
      continue;
    }
    if (c.host_only) {
      if (host_norm != c.domain) {
        continue;
      }
    } else {
      if (!DomainMatches(host_norm, c.domain)) {
        continue;
      }
    }
    if (!PathMatches(request_path, c.path)) {
      continue;
    }
    candidates.push_back({i, c.path.size()});
  }

  if (candidates.empty()) {
    return {};
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) {
              return a.path_len > b.path_len;
            });

  std::string out;
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const Cookie& c = cookies_[candidates[i].idx];
    if (c.name.empty()) {
      continue;
    }
    if (!out.empty()) {
      out.append("; ");
    }
    out.append(c.name);
    out.push_back('=');
    out.append(c.value);
  }
  return out;
}

void HttpClientSession::UpsertFromSetCookieLocked(
    std::string_view host, std::string_view target,
    std::string_view set_cookie_value) {
  set_cookie_value = TrimView(set_cookie_value);
  if (set_cookie_value.empty()) {
    return;
  }

  // Split by ';'
  std::vector<std::string_view> tokens;
  std::size_t start = 0;
  while (start < set_cookie_value.size()) {
    auto semi = set_cookie_value.find(';', start);
    if (semi == std::string_view::npos) {
      tokens.push_back(TrimView(set_cookie_value.substr(start)));
      break;
    }
    tokens.push_back(TrimView(set_cookie_value.substr(start, semi - start)));
    start = semi + 1;
  }
  if (tokens.empty() || tokens[0].empty()) {
    return;
  }

  auto [nv_name, nv_value] = SplitOnce(tokens[0], '=');
  nv_name = TrimView(nv_name);
  nv_value = TrimView(nv_value);
  if (nv_name.empty()) {
    return;
  }

  Cookie incoming;
  incoming.name = std::string(nv_name);
  incoming.value = std::string(nv_value);

  const std::string host_norm = NormalizeHost(host);
  incoming.domain = host_norm;
  incoming.host_only = true;
  incoming.path = DefaultPathFromTarget(target);

  bool has_domain_attr = false;
  bool has_path_attr = false;
  bool has_max_age = false;
  bool max_age_valid = false;
  long long max_age_secs = 0;
  std::optional<std::chrono::system_clock::time_point> expires_tp;

  for (std::size_t i = 1; i < tokens.size(); ++i) {
    auto token = tokens[i];
    if (token.empty()) {
      continue;
    }
    auto [k, v] = SplitOnce(token, '=');
    k = TrimView(k);
    v = TrimView(v);

    if (IEquals(k, "Secure")) {
      incoming.secure = true;
      continue;
    }
    if (IEquals(k, "HttpOnly") || IEquals(k, "SameSite")) {
      // Ignored for client sending logic.
      continue;
    }
    if (IEquals(k, "Domain")) {
      const std::string dom = NormalizeDomain(v);
      if (!dom.empty() && DomainMatches(host_norm, dom)) {
        incoming.domain = dom;
        incoming.host_only = false;
        has_domain_attr = true;
      } else {
        // Domain attribute invalid for this host; ignore the whole cookie.
        return;
      }
      continue;
    }
    if (IEquals(k, "Path")) {
      if (!v.empty() && v.front() == '/') {
        incoming.path = std::string(v);
        has_path_attr = true;
      }
      continue;
    }
    if (IEquals(k, "Max-Age")) {
      has_max_age = true;
      try {
        max_age_secs = std::stoll(std::string(v));
        max_age_valid = true;
      } catch (...) {
        max_age_valid = false;
      }
      continue;
    }
    if (IEquals(k, "Expires")) {
      expires_tp = ParseImfFixdate(v);
      continue;
    }
  }

  // Ensure default path if Path attr was absent.
  (void)has_path_attr;
  (void)has_domain_attr;

  // Compute expiry.
  if (has_max_age && max_age_valid) {
    if (max_age_secs <= 0) {
      // Deletion.
      cookies_.erase(std::remove_if(cookies_.begin(), cookies_.end(),
                                    [&](const Cookie& c) {
                                      return c.name == incoming.name &&
                                             c.domain == incoming.domain &&
                                             c.host_only ==
                                                 incoming.host_only &&
                                             c.path == incoming.path;
                                    }),
                     cookies_.end());
      return;
    }
    incoming.expiry =
        std::chrono::system_clock::now() + std::chrono::seconds(max_age_secs);
  } else if (expires_tp.has_value()) {
    incoming.expiry = expires_tp;
    if (incoming.expiry.value() <= std::chrono::system_clock::now()) {
      // Treat already-expired as deletion.
      cookies_.erase(std::remove_if(cookies_.begin(), cookies_.end(),
                                    [&](const Cookie& c) {
                                      return c.name == incoming.name &&
                                             c.domain == incoming.domain &&
                                             c.host_only ==
                                                 incoming.host_only &&
                                             c.path == incoming.path;
                                    }),
                     cookies_.end());
      return;
    }
  }

  // Upsert by (name, domain, host_only, path).
  for (auto& c : cookies_) {
    if (c.name == incoming.name && c.domain == incoming.domain &&
        c.host_only == incoming.host_only && c.path == incoming.path) {
      c.value = std::move(incoming.value);
      c.secure = incoming.secure;
      c.expiry = incoming.expiry;
      return;
    }
  }

  cookies_.push_back(std::move(incoming));
}

}  // namespace bsrvcore
