/**
 * @file request_assembler.h
 * @brief Request assembly and connection identity abstraction for HTTP client.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-26
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_REQUEST_ASSEMBLER_H_
#define BSRVCORE_CONNECTION_CLIENT_REQUEST_ASSEMBLER_H_

#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/stream_builder.h"
#include "bsrvcore/connection/client/stream_slot.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"

namespace bsrvcore {

/**
 * @brief Abstract assembler that builds a final HTTP request and determines
 * the connection identity (ConnectionKey) required to serve it.
 *
 * Concrete implementations customize request preparation, header injection
 * (e.g. cookies), and SSL context selection.
 *
 * Note: RequestAssembler does NOT inherit enable_shared_from_this. Subclasses
 * that need shared_from_this should inherit it themselves.
 */
class RequestAssembler : public NonCopyableNonMovable<RequestAssembler> {
 public:
  /** @brief Result of assembly: a ready request and its connection key. */
  struct AssemblyResult {
    HttpClientRequest request;
    ConnectionKey connection_key;
  };

  ~RequestAssembler() = default;

  /**
   * @brief Assemble a request with the given connection identity.
   *
   * Implementations may ignore the identity parameters (DefaultRequestAssembler
   * uses its own stored identity) or use them (SessionRequestAssembler for
   * multi-host sessions).
   *
   * The returned request has host/user-agent/keep-alive headers filled and
   * prepare_payload() already called.
   */
  virtual AssemblyResult Assemble(HttpClientRequest request,
                                  const HttpClientOptions& options,
                                  std::string_view scheme,
                                  std::string_view host, std::string_view port,
                                  SslContextPtr ssl_ctx) = 0;

  /**
   * @brief Called by HttpClientTask when response header is received.
   *
   * Default implementation does nothing (no cookie handling).
   * SessionRequestAssembler overrides this to sync Set-Cookie headers.
   */
  virtual void OnResponseHeader(const HttpResponseHeader& header,
                                std::string_view host, std::string_view target);

  /**
   * @brief Attach a StreamBuilder to this Assembler.
   */
  void SetStreamBuilder(std::shared_ptr<StreamBuilder> builder);

 protected:
  RequestAssembler() = default;

  std::shared_ptr<StreamBuilder> stream_builder_;
};

/**
 * @brief Simple assembler with fixed connection identity.
 *
 * Sets standard request headers and derives ConnectionKey from the
 * provider-supplied scheme/host/port/ssl_ctx at construction time.
 */
class DefaultRequestAssembler : public RequestAssembler {
 public:
  /**
   * @brief Construct with connection identity.
   *
   * @param scheme "http" or "https"
   * @param host Target hostname
   * @param port Target port string
   * @param ssl_ctx SSL context (nullptr for http)
   */
  DefaultRequestAssembler(std::string scheme, std::string host,
                          std::string port, SslContextPtr ssl_ctx = {});

  AssemblyResult Assemble(HttpClientRequest request,
                          const HttpClientOptions& options,
                          std::string_view scheme, std::string_view host,
                          std::string_view port,
                          SslContextPtr ssl_ctx) override;

 protected:
  std::string scheme_;
  std::string host_;
  std::string port_;
  SslContextPtr ssl_ctx_;
};

/**
 * @brief Session-aware assembler with cookie jar.
 *
 * Unlike DefaultRequestAssembler, this assembler does NOT have a fixed
 * connection identity. Each Assemble() call takes the identity as parameters,
 * making it suitable for HttpClientSession which can make requests to
 * multiple hosts.
 *
 * Provides cookie injection before prepare_payload() and Set-Cookie
 * synchronization via a header hook.
 */
class SessionRequestAssembler : public RequestAssembler {
 public:
  AssemblyResult Assemble(HttpClientRequest request,
                          const HttpClientOptions& options,
                          std::string_view scheme, std::string_view host,
                          std::string_view port,
                          SslContextPtr ssl_ctx) override;

  /**
   * @brief Override: sync Set-Cookie headers from response into the jar.
   */
  void OnResponseHeader(const HttpResponseHeader& header, std::string_view host,
                        std::string_view target) override;

  // ---- Cookie jar API ----

  /** @brief Inject cookies into request if Cookie header is absent. */
  void MaybeInjectCookies(HttpClientRequest& request, std::string_view host,
                          std::string_view target, bool is_https);

  /** @brief Process one Set-Cookie header value and update the jar. */
  void SyncSetCookie(std::string_view host, std::string_view target,
                     std::string_view set_cookie_value);

  /** @brief Remove all stored cookies. */
  void ClearCookies();

  /** @brief Current cookie count (after expired cleanup). */
  std::size_t CookieCount() const;

 protected:
  struct Cookie {
    std::string name;
    std::string value;
    std::string domain;
    bool host_only{true};
    std::string path{"/"};
    bool secure{false};
    std::optional<std::chrono::system_clock::time_point> expiry;
  };

  /** @brief Parsed expiry info from Set-Cookie attributes. */
  struct CookieExpiryInfo {
    bool has_max_age = false;
    bool max_age_valid = false;
    long long max_age_secs = 0;
    std::optional<std::chrono::system_clock::time_point> expires_tp;
  };

  using CookieStorage = AllocatedVector<Cookie>;

  void CleanupExpiredLocked() const;

  static std::string NormalizeHost(std::string_view host);
  static std::string NormalizeDomain(std::string_view domain);
  static std::string DefaultPathFromTarget(std::string_view target);
  static bool DomainMatches(std::string_view host, std::string_view domain);
  static bool PathMatches(std::string_view request_path,
                          std::string_view cookie_path);

  std::string BuildCookieHeaderLocked(std::string_view host,
                                      std::string_view target,
                                      bool is_https) const;

  void UpsertFromSetCookieLocked(std::string_view host, std::string_view target,
                                 std::string_view set_cookie_value);

 private:
  /**
   * @brief Collect indices of cookies matching the given host/path/secure.
   *
   * Call chain: BuildCookieHeaderLocked → CollectMatchingCookiesLocked
   *
   * Returns pairs of (index, path_length) for sorting by path specificity.
   */
  struct CookieCandidate {
    std::size_t index;
    std::size_t path_len;
  };
  using CandidateList = AllocatedVector<CookieCandidate>;

  [[nodiscard]] CandidateList CollectMatchingCookiesLocked(
      std::string_view host_norm, std::string_view request_path,
      bool is_https) const;

  /**
   * @brief Serialize sorted candidates into "name=value; name=value" string.
   *
   * Call chain: BuildCookieHeaderLocked → SerializeCookieCandidates
   */
  [[nodiscard]] std::string SerializeCookieCandidates(
      const CandidateList& candidates) const;

  /**
   * @brief Parse a Set-Cookie header value into a Cookie struct.
   *
   * Call chain: SyncSetCookie → UpsertFromSetCookieLocked → ParseSetCookieValue
   *
   * Returns nullopt when the value is malformed or the cookie is expired
   * (Max-Age<=0 or Expires in the past).
   */
  std::optional<Cookie> ParseSetCookieValue(std::string_view host,
                                            std::string_view target,
                                            std::string_view value) const;

  /**
   * @brief Insert or update a parsed cookie in the jar.
   *
   * Call chain: UpsertFromSetCookieLocked → UpsertCookieLocked
   *
   * Matches on (name, domain, host_only, path). Updates value/secure/expiry
   * in-place if found; appends otherwise.
   */
  void UpsertCookieLocked(Cookie incoming);

  /**
   * @brief Apply one Set-Cookie attribute to a cookie.
   *
   * Call chain: ParseSetCookieValue → ApplySetCookieAttribute (per attribute)
   *
   * Returns false when the attribute is invalid and the cookie should be
   * rejected entirely (e.g. invalid Domain).
   */
  bool ApplySetCookieAttribute(std::string_view key, std::string_view value,
                               Cookie& cookie, std::string_view host_norm,
                               CookieExpiryInfo& expiry) const;

  /**
   * @brief Compute final expiry from Max-Age/Expires and apply to cookie.
   *
   * Call chain: ParseSetCookieValue → ComputeAndApplyExpiry
   *
   * Returns false when the cookie is expired (should be deleted).
   */
  static bool ComputeAndApplyExpiry(Cookie& cookie,
                                    const CookieExpiryInfo& expiry);

  mutable std::mutex mutex_;
  mutable CookieStorage cookies_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_REQUEST_ASSEMBLER_H_
