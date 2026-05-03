/**
 * @file request_assembler.cc
 * @brief DefaultRequestAssembler and SessionRequestAssembler implementations.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-26
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/client/request_assembler.h"

#include <algorithm>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <locale>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/stream_slot.h"
#include "bsrvcore/core/types.h"

namespace bsrvcore {

namespace http = boost::beast::http;

namespace {

using CookieTokenViews = AllocatedVector<std::string_view>;

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
    unsigned char const ca = static_cast<unsigned char>(a[i]);
    unsigned char const cb = static_cast<unsigned char>(b[i]);
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

inline CookieTokenViews SplitSetCookieTokens(
    std::string_view set_cookie_value) {
  CookieTokenViews tokens;
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
  return tokens;
}

inline std::optional<std::chrono::system_clock::time_point> ParseImfFixdate(
    std::string_view v) {
  std::tm tm{};
  std::istringstream iss{std::string(v)};
  iss.imbue(std::locale::classic());
  iss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
  if (iss.fail()) {
    return std::nullopt;
  }

  const std::chrono::year_month_day date{
      std::chrono::year{tm.tm_year + 1900},
      std::chrono::month{static_cast<unsigned>(tm.tm_mon + 1)},
      std::chrono::day{static_cast<unsigned>(tm.tm_mday)}};
  if (!date.ok() || tm.tm_hour < 0 || tm.tm_hour > 23 || tm.tm_min < 0 ||
      tm.tm_min > 59 || tm.tm_sec < 0 || tm.tm_sec > 59) {
    return std::nullopt;
  }

  const auto timestamp =
      std::chrono::sys_days{date} + std::chrono::hours{tm.tm_hour} +
      std::chrono::minutes{tm.tm_min} + std::chrono::seconds{tm.tm_sec};
  return std::chrono::system_clock::time_point{timestamp};
}

}  // namespace

void RequestAssembler::OnResponseHeader(const HttpResponseHeader& /*header*/,
                                        std::string_view /*host*/,
                                        std::string_view /*target*/) {
  // Default: no-op. Subclasses override for cookie sync, etc.
}

// ---- DefaultRequestAssembler ----

RequestAssembler::AssemblyResult DefaultRequestAssembler::Assemble(
    HttpClientRequest request, const HttpClientOptions& options,
    std::string_view scheme, std::string_view host, std::string_view port,
    SslContextPtr ssl_ctx) {
  if (request.find(http::field::host) == request.end()) {
    request.set(http::field::host, std::string(host));
  }
  if (request.find(http::field::user_agent) == request.end() &&
      !options.user_agent.empty()) {
    request.set(http::field::user_agent, options.user_agent);
  }
  request.keep_alive(options.keep_alive);

  ConnectionKey key;
  key.scheme = std::string(scheme);
  key.host = std::string(host);
  key.port = std::string(port);
  key.ssl_ctx = ssl_ctx;
  key.verify_peer = options.verify_peer;

  request.prepare_payload();

  return {std::move(request), std::move(key)};
}

// ---- SessionRequestAssembler ----

RequestAssembler::AssemblyResult SessionRequestAssembler::Assemble(
    HttpClientRequest request, const HttpClientOptions& options,
    std::string_view scheme, std::string_view host, std::string_view port,
    SslContextPtr ssl_ctx) {
  if (request.find(http::field::host) == request.end()) {
    request.set(http::field::host, std::string(host));
  }
  if (request.find(http::field::user_agent) == request.end() &&
      !options.user_agent.empty()) {
    request.set(http::field::user_agent, options.user_agent);
  }
  request.keep_alive(options.keep_alive);

  // Cookie injection.
  MaybeInjectCookies(request, host, request.target(), scheme == "https");

  request.prepare_payload();

  ConnectionKey key;
  key.scheme = std::string(scheme);
  key.host = std::string(host);
  key.port = std::string(port);
  key.ssl_ctx = ssl_ctx;
  key.verify_peer = options.verify_peer;

  return {std::move(request), std::move(key)};
}

void SessionRequestAssembler::MaybeInjectCookies(HttpClientRequest& request,
                                                 std::string_view host,
                                                 std::string_view target,
                                                 bool is_https) {
  if (request.find(http::field::cookie) != request.end()) {
    return;
  }

  std::string const cookie = [&]() {
    std::scoped_lock const lock(mutex_);
    CleanupExpiredLocked();
    return BuildCookieHeaderLocked(host, target, is_https);
  }();

  if (!cookie.empty()) {
    request.set(http::field::cookie, cookie);
  }
}

void SessionRequestAssembler::SyncSetCookie(std::string_view host,
                                            std::string_view target,
                                            std::string_view set_cookie_value) {
  std::scoped_lock const lock(mutex_);
  CleanupExpiredLocked();
  UpsertFromSetCookieLocked(host, target, set_cookie_value);
}

void SessionRequestAssembler::ClearCookies() {
  std::scoped_lock const lock(mutex_);
  cookies_.clear();
}

std::size_t SessionRequestAssembler::CookieCount() const {
  std::scoped_lock const lock(mutex_);
  CleanupExpiredLocked();
  return cookies_.size();
}

void SessionRequestAssembler::OnResponseHeader(const HttpResponseHeader& header,
                                               std::string_view host,
                                               std::string_view target) {
  auto range = header.equal_range(HttpField::set_cookie);
  for (auto it = range.first; it != range.second; ++it) {
    SyncSetCookie(host, target, it->value());
  }
}

void SessionRequestAssembler::CleanupExpiredLocked() const {
  const auto now = std::chrono::system_clock::now();
  std::erase_if(cookies_, [&](const Cookie& c) {
    return c.expiry.has_value() && c.expiry.value() <= now;
  });
}

std::string SessionRequestAssembler::NormalizeHost(std::string_view host) {
  std::string out;
  out.reserve(host.size());
  for (unsigned char const c : host) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

std::string SessionRequestAssembler::NormalizeDomain(std::string_view domain) {
  domain = TrimView(domain);
  if (!domain.empty() && domain.front() == '.') {
    domain.remove_prefix(1);
  }
  return NormalizeHost(domain);
}

std::string SessionRequestAssembler::DefaultPathFromTarget(
    std::string_view target) {
  auto q = target.find('?');
  std::string_view const path =
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

bool SessionRequestAssembler::DomainMatches(std::string_view host,
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
  return host[offset - 1] == '.';
}

bool SessionRequestAssembler::PathMatches(std::string_view request_path,
                                          std::string_view cookie_path) {
  if (cookie_path.empty() || cookie_path.front() != '/') {
    cookie_path = "/";
  }
  if (cookie_path == "/") {
    return true;
  }
  if (!request_path.starts_with(cookie_path)) {
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

std::string SessionRequestAssembler::BuildCookieHeaderLocked(
    std::string_view host, std::string_view target, bool is_https) const {
  // Call chain: MaybeInjectCookies → BuildCookieHeaderLocked
  //   → CollectMatchingCookiesLocked (filter by domain/path/secure)
  //   → SerializeCookieCandidates    (sort by path, build string)
  const std::string host_norm = NormalizeHost(host);
  auto q = target.find('?');
  const std::string_view request_path =
      (q == std::string_view::npos) ? target : target.substr(0, q);

  auto candidates =
      CollectMatchingCookiesLocked(host_norm, request_path, is_https);
  if (candidates.empty()) {
    return {};
  }

  // Sort by path length descending (more specific paths first).
  std::ranges::sort(candidates,
                    [](const CookieCandidate& a, const CookieCandidate& b) {
                      return a.path_len > b.path_len;
                    });

  return SerializeCookieCandidates(candidates);
}

SessionRequestAssembler::CandidateList
SessionRequestAssembler::CollectMatchingCookiesLocked(
    std::string_view host_norm, std::string_view request_path,
    bool is_https) const {
  CandidateList candidates;
  candidates.reserve(cookies_.size());

  for (std::size_t i = 0; i < cookies_.size(); ++i) {
    const Cookie& c = cookies_[i];
    // Secure cookies only sent over HTTPS.
    if (c.secure && !is_https) {
      continue;
    }
    // Domain matching: host-only requires exact match; otherwise suffix match.
    if (c.host_only) {
      if (host_norm != c.domain) {
        continue;
      }
    } else {
      if (!DomainMatches(host_norm, c.domain)) {
        continue;
      }
    }
    // Path matching per RFC 6265 Section 5.1.4.
    if (!PathMatches(request_path, c.path)) {
      continue;
    }
    candidates.push_back({i, c.path.size()});
  }

  return candidates;
}

std::string SessionRequestAssembler::SerializeCookieCandidates(
    const CandidateList& candidates) const {
  std::string out;
  for (const auto& candidate : candidates) {
    const Cookie& c = cookies_[candidate.index];
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

void SessionRequestAssembler::UpsertFromSetCookieLocked(
    std::string_view host, std::string_view target,
    std::string_view set_cookie_value) {
  // Call chain: SyncSetCookie → UpsertFromSetCookieLocked
  //   → ParseSetCookieValue (parse Set-Cookie string into Cookie struct)
  //   → UpsertCookieLocked  (insert or update in jar)
  auto cookie = ParseSetCookieValue(host, target, set_cookie_value);
  if (!cookie.has_value()) {
    return;
  }

  // Max-Age<=0 or expired Expires means delete the cookie.
  if (cookie->value.empty() && cookie->expiry.has_value() &&
      cookie->expiry.value() <= std::chrono::system_clock::now()) {
    std::erase_if(cookies_, [&](const Cookie& c) {
      return c.name == cookie->name && c.domain == cookie->domain &&
             c.host_only == cookie->host_only && c.path == cookie->path;
    });
    return;
  }

  UpsertCookieLocked(std::move(*cookie));
}

// ---- Private helpers ----

bool SessionRequestAssembler::ApplySetCookieAttribute(
    std::string_view key, std::string_view value, Cookie& cookie,
    std::string_view host_norm, CookieExpiryInfo& expiry) const {
  if (IEquals(key, "Secure")) {
    cookie.secure = true;
    return true;
  }
  if (IEquals(key, "HttpOnly") || IEquals(key, "SameSite")) {
    return true;  // Ignored for matching purposes.
  }
  if (IEquals(key, "Domain")) {
    const std::string dom = NormalizeDomain(value);
    if (!dom.empty() && DomainMatches(host_norm, dom)) {
      cookie.domain = dom;
      cookie.host_only = false;
      return true;
    }
    return false;  // Invalid domain → reject entire cookie.
  }
  if (IEquals(key, "Path")) {
    if (!value.empty() && value.front() == '/') {
      cookie.path = std::string(value);
    }
    return true;
  }
  if (IEquals(key, "Max-Age")) {
    expiry.has_max_age = true;
    try {
      expiry.max_age_secs = std::stoll(std::string(value));
      expiry.max_age_valid = true;
    } catch (...) {
      expiry.max_age_valid = false;
    }
    return true;
  }
  if (IEquals(key, "Expires")) {
    expiry.expires_tp = ParseImfFixdate(value);
    return true;
  }
  return true;  // Unknown attributes are ignored per RFC 6265.
}

bool SessionRequestAssembler::ComputeAndApplyExpiry(
    Cookie& cookie, const CookieExpiryInfo& expiry) {
  if (expiry.has_max_age && expiry.max_age_valid) {
    if (expiry.max_age_secs <= 0) {
      cookie.value.clear();
      cookie.expiry = std::chrono::system_clock::time_point{};
      return false;  // Signal deletion.
    }
    cookie.expiry = std::chrono::system_clock::now() +
                    std::chrono::seconds(expiry.max_age_secs);
    return true;
  }
  if (expiry.expires_tp.has_value()) {
    cookie.expiry = expiry.expires_tp;
    if (cookie.expiry.value() <= std::chrono::system_clock::now()) {
      cookie.value.clear();
      return false;  // Signal deletion.
    }
    return true;
  }
  return true;  // No expiry → session cookie.
}

std::optional<SessionRequestAssembler::Cookie>
SessionRequestAssembler::ParseSetCookieValue(std::string_view host,
                                             std::string_view target,
                                             std::string_view value) const {
  // Call chain: UpsertFromSetCookieLocked → ParseSetCookieValue
  //   → SplitSetCookieTokens   (tokenize "name=value; attr=val; ...")
  //   → ApplySetCookieAttribute (per attribute: Domain/Path/Max-Age/...)
  //   → ComputeAndApplyExpiry   (Max-Age > Expires precedence)
  value = TrimView(value);
  if (value.empty()) {
    return std::nullopt;
  }

  const CookieTokenViews tokens = SplitSetCookieTokens(value);
  if (tokens.empty() || tokens[0].empty()) {
    return std::nullopt;
  }

  // First token is always name=value.
  auto [nv_name, nv_value] = SplitOnce(tokens[0], '=');
  nv_name = TrimView(nv_name);
  nv_value = TrimView(nv_value);
  if (nv_name.empty()) {
    return std::nullopt;
  }

  Cookie cookie;
  cookie.name = std::string(nv_name);
  cookie.value = std::string(nv_value);

  // Defaults per RFC 6265.
  const std::string host_norm = NormalizeHost(host);
  cookie.domain = host_norm;
  cookie.host_only = true;
  cookie.path = DefaultPathFromTarget(target);

  // Parse attributes.
  CookieExpiryInfo expiry;
  for (std::size_t i = 1; i < tokens.size(); ++i) {
    auto token = tokens[i];
    if (token.empty()) {
      continue;
    }
    auto [k, v] = SplitOnce(token, '=');
    k = TrimView(k);
    v = TrimView(v);
    if (!ApplySetCookieAttribute(k, v, cookie, host_norm, expiry)) {
      return std::nullopt;  // Invalid attribute → reject cookie.
    }
  }

  // Compute and apply expiry.
  if (!ComputeAndApplyExpiry(cookie, expiry)) {
    return cookie;  // Expired — caller will delete.
  }

  return cookie;
}

void SessionRequestAssembler::UpsertCookieLocked(Cookie incoming) {
  // Match on (name, domain, host_only, path) — update in-place if found.
  for (auto& c : cookies_) {
    if (c.name == incoming.name && c.domain == incoming.domain &&
        c.host_only == incoming.host_only && c.path == incoming.path) {
      c.value = std::move(incoming.value);
      c.secure = incoming.secure;
      c.expiry = incoming.expiry;
      return;
    }
  }

  // Not found — append new cookie.
  cookies_.push_back(std::move(incoming));
}

// ---- ProxyRequestAssembler ----

ProxyRequestAssembler::ProxyRequestAssembler(
    std::shared_ptr<RequestAssembler> inner, ProxyConfig proxy)
    : inner_(std::move(inner)), proxy_(std::move(proxy)) {}

RequestAssembler::AssemblyResult ProxyRequestAssembler::Assemble(
    HttpClientRequest request, const HttpClientOptions& options,
    std::string_view scheme, std::string_view host, std::string_view port,
    SslContextPtr ssl_ctx) {
  // Delegate to inner assembler first.
  auto result = inner_->Assemble(request, options, scheme, host, port, ssl_ctx);

  if (!proxy_.enabled()) {
    return result;
  }

  const bool is_https = (result.connection_key.scheme == "https");

  if (!is_https) {
    // HTTP proxy: rewrite target to absolute-form.
    // E.g. GET /path → GET http://target:port/path
    const std::string original_target(result.request.target());
    const std::string original_host(result.connection_key.host);
    const std::string original_port(result.connection_key.port);

    std::string absolute_url = "http://" + original_host;
    if (original_port != "80") {
      absolute_url += ":" + original_port;
    }
    absolute_url += original_target;
    result.request.target(absolute_url);
  }

  // Both HTTP and HTTPS proxy: ConnectionKey points to the proxy server.
  // For HTTPS, ssl_ctx is cleared so ProxyStreamBuilder does CONNECT first.
  result.connection_key.proxy_host = proxy_.host;
  result.connection_key.proxy_port = proxy_.port;

  if (is_https) {
    // HTTPS proxy: connection goes to proxy in plain TCP first,
    // then CONNECT tunnel, then TLS. ProxyStreamBuilder handles this.
    // Preserve the original SSL context for TLS handshake on the tunnel.
    result.connection_key.proxy_ssl_ctx = result.connection_key.ssl_ctx;
    result.connection_key.ssl_ctx = nullptr;
  }

  // Inject Proxy-Authorization header if configured.
  if (!proxy_.auth.empty()) {
    result.request.set(http::field::proxy_authorization, proxy_.auth);
  }

  return result;
}

void ProxyRequestAssembler::OnResponseHeader(const HttpResponseHeader& header,
                                             std::string_view host,
                                             std::string_view target) {
  inner_->OnResponseHeader(header, host, target);
}

}  // namespace bsrvcore
