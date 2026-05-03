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

#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/http_client_task.h"
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
    /** @brief Fully prepared HTTP request to send. */
    HttpClientRequest request;
    /** @brief Connection identity required to serve the request. */
    ConnectionKey connection_key;
  };

  virtual ~RequestAssembler() = default;

  /**
   * @brief Assemble a request with the given connection identity.
   *
   * Implementations may ignore the identity parameters (DefaultRequestAssembler
   * uses its own stored identity) or use them (SessionRequestAssembler for
   * multi-host sessions).
   *
   * The returned request has host/user-agent/keep-alive headers filled and
   * prepare_payload() already called.
   *
   * @param request Request object to complete and prepare for transmission.
   * @param options Per-request client options used for headers and transport
   * selection.
   * @param scheme Connection scheme, normally `http`, `https`, `ws`, or `wss`.
   * @param host Target host name used for request headers and connection key.
   * @param port Target service port used for the connection key.
   * @param ssl_ctx TLS context for TLS-backed schemes, or null for plain TCP.
   * @return Prepared request and the connection identity required to send it.
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
   *
   * @param header Response header received from the server.
   * @param host Host name associated with the response.
   * @param target Request target associated with the response.
   */
  virtual void OnResponseHeader(const HttpResponseHeader& header,
                                std::string_view host, std::string_view target);

 protected:
  RequestAssembler() = default;
};

/**
 * @brief Stateless default assembler.
 *
 * Sets standard request headers and derives ConnectionKey from the
 * parameters passed to Assemble().
 */
class DefaultRequestAssembler : public RequestAssembler {
 public:
  DefaultRequestAssembler() = default;
  ~DefaultRequestAssembler() override = default;

  AssemblyResult Assemble(HttpClientRequest request,
                          const HttpClientOptions& options,
                          std::string_view scheme, std::string_view host,
                          std::string_view port,
                          SslContextPtr ssl_ctx) override;
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
   *
   * @param header Response header that may contain Set-Cookie fields.
   * @param host Host name used for cookie domain matching.
   * @param target Request target used for cookie path matching.
   */
  void OnResponseHeader(const HttpResponseHeader& header, std::string_view host,
                        std::string_view target) override;

  // ---- Cookie jar API ----

  /**
   * @brief Inject cookies into request if Cookie header is absent.
   *
   * @param request Request to update in-place.
   * @param host Request host name used for domain matching.
   * @param target Request target used for path matching.
   * @param is_https Whether the request will use a TLS-backed connection.
   */
  void MaybeInjectCookies(HttpClientRequest& request, std::string_view host,
                          std::string_view target, bool is_https);

  /**
   * @brief Process one Set-Cookie header value and update the jar.
   *
   * @param host Response host name used to normalize host-only cookies.
   * @param target Request target used to derive the default cookie path.
   * @param set_cookie_value Raw Set-Cookie field value.
   */
  void SyncSetCookie(std::string_view host, std::string_view target,
                     std::string_view set_cookie_value);

  /** @brief Remove all stored cookies. */
  void ClearCookies();

  /**
   * @brief Current cookie count (after expired cleanup).
   *
   * @return Number of non-expired cookies currently stored in the jar.
   */
  std::size_t CookieCount() const;

 protected:
  /** @brief Stored cookie metadata normalized for matching future requests. */
  struct Cookie {
    /** @brief Cookie name. */
    std::string name;
    /** @brief Cookie value. */
    std::string value;
    /** @brief Normalized cookie domain. */
    std::string domain;
    /** @brief Whether the cookie is restricted to the original host. */
    bool host_only{true};
    /** @brief Request path prefix for which the cookie is valid. */
    std::string path{"/"};
    /** @brief Whether the cookie can only be sent over HTTPS. */
    bool secure{false};
    /** @brief Optional absolute expiration time. */
    std::optional<std::chrono::system_clock::time_point> expiry;
  };

  /** @brief Parsed expiry info from Set-Cookie attributes. */
  struct CookieExpiryInfo {
    /** @brief Whether a Max-Age attribute was present. */
    bool has_max_age = false;
    /** @brief Whether the Max-Age attribute parsed successfully. */
    bool max_age_valid = false;
    /** @brief Parsed Max-Age value in seconds. */
    long long max_age_secs = 0;
    /** @brief Parsed Expires timestamp, when present and valid. */
    std::optional<std::chrono::system_clock::time_point> expires_tp;
  };

  /** @brief Allocator-backed cookie jar storage. */
  using CookieStorage = AllocatedVector<Cookie>;

  /** @brief Remove expired cookies while the cookie mutex is held. */
  void CleanupExpiredLocked() const;

  /**
   * @brief Normalize a host for cookie domain comparisons.
   *
   * @param host Host name to normalize.
   * @return Lowercase host name without cookie-domain decoration.
   */
  static std::string NormalizeHost(std::string_view host);
  /**
   * @brief Normalize a cookie domain attribute.
   *
   * @param domain Cookie Domain attribute value to normalize.
   * @return Lowercase domain without a leading dot.
   */
  static std::string NormalizeDomain(std::string_view domain);
  /**
   * @brief Derive a default cookie path from a request target.
   *
   * @param target Request target path or URI.
   * @return Default cookie path according to request-target path rules.
   */
  static std::string DefaultPathFromTarget(std::string_view target);
  /**
   * @brief Return true when host matches a cookie domain.
   *
   * @param host Normalized request host.
   * @param domain Normalized cookie domain.
   * @return True when the host is equal to or under the cookie domain.
   */
  static bool DomainMatches(std::string_view host, std::string_view domain);
  /**
   * @brief Return true when request path matches a cookie path.
   *
   * @param request_path Path component from the request target.
   * @param cookie_path Cookie path attribute or derived default path.
   * @return True when the cookie should be sent for the request path.
   */
  static bool PathMatches(std::string_view request_path,
                          std::string_view cookie_path);

  /**
   * @brief Build the Cookie request header while the cookie mutex is held.
   *
   * @param host Normalized request host.
   * @param target Request target used for path matching.
   * @param is_https Whether secure cookies are eligible for the request.
   * @return Cookie header value, or an empty string when nothing matches.
   */
  std::string BuildCookieHeaderLocked(std::string_view host,
                                      std::string_view target,
                                      bool is_https) const;

  /**
   * @brief Parse and merge one Set-Cookie value while the mutex is held.
   *
   * @param host Response host name used to normalize host-only cookies.
   * @param target Request target used to derive default paths.
   * @param set_cookie_value Raw Set-Cookie field value to parse.
   */
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
    /** @brief Index into the cookie storage. */
    std::size_t index;
    /** @brief Cookie path length used to sort by specificity. */
    std::size_t path_len;
  };
  /** @brief Allocator-backed list of matching cookie candidates. */
  using CandidateList = AllocatedVector<CookieCandidate>;

  /**
   * @brief Collect matching cookie candidates while the cookie mutex is held.
   *
   * @param host_norm Normalized request host.
   * @param request_path Request path used for cookie path matching.
   * @param is_https Whether secure cookies are eligible.
   * @return Matching cookie candidates with specificity metadata.
   */
  [[nodiscard]] CandidateList CollectMatchingCookiesLocked(
      std::string_view host_norm, std::string_view request_path,
      bool is_https) const;

  /**
   * @brief Serialize sorted candidates into "name=value; name=value" string.
   *
   * Call chain: BuildCookieHeaderLocked → SerializeCookieCandidates
   *
   * @param candidates Cookie candidates to serialize.
   * @return Cookie header value.
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
   *
   * @param host Response host name used for host-only cookies.
   * @param target Request target used to derive the default path.
   * @param value Raw Set-Cookie field value.
   * @return Parsed cookie, or nullopt when the value is rejected.
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
   *
   * @param incoming Parsed cookie to insert or merge.
   */
  void UpsertCookieLocked(Cookie incoming);

  /**
   * @brief Apply one Set-Cookie attribute to a cookie.
   *
   * Call chain: ParseSetCookieValue → ApplySetCookieAttribute (per attribute)
   *
   * Returns false when the attribute is invalid and the cookie should be
   * rejected entirely (e.g. invalid Domain).
   *
   * @param key Lowercase Set-Cookie attribute key.
   * @param value Attribute value, or empty for flag attributes.
   * @param cookie Cookie being updated.
   * @param host_norm Normalized response host.
   * @param expiry Parsed expiry accumulator to update.
   * @return True when parsing may continue.
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
   *
   * @param cookie Cookie whose expiry should be updated.
   * @param expiry Parsed Max-Age/Expires information.
   * @return True when the cookie remains valid.
   */
  static bool ComputeAndApplyExpiry(Cookie& cookie,
                                    const CookieExpiryInfo& expiry);

  mutable std::mutex mutex_;
  mutable CookieStorage cookies_;
};

/**
 * @brief Decorator that routes requests through an HTTP/HTTPS proxy.
 *
 * Wraps an inner RequestAssembler and modifies the assembly result:
 *
 * - **HTTP proxy**: Rewrites the request target to absolute-form
 *   (e.g. "http://target/path") and sets ConnectionKey to the proxy
 *   server. The proxy forwards the request to the actual target.
 *
 * - **HTTPS proxy**: Keeps the target as-is but sets ConnectionKey to
 *   the proxy server with ssl_ctx=nullptr. ProxyStreamBuilder then
 *   establishes a CONNECT tunnel and performs TLS through it.
 *
 * If the proxy is not enabled (proxy.host is empty), the inner
 * assembler's result is returned unchanged.
 */
class ProxyRequestAssembler : public RequestAssembler {
 public:
  /**
   * @brief Construct with an inner assembler and proxy configuration.
   *
   * @param inner Inner assembler to delegate to.
   * @param proxy Proxy configuration (host, port, auth).
   */
  ProxyRequestAssembler(std::shared_ptr<RequestAssembler> inner,
                        ProxyConfig proxy);

  ~ProxyRequestAssembler() override = default;

  AssemblyResult Assemble(HttpClientRequest request,
                          const HttpClientOptions& options,
                          std::string_view scheme, std::string_view host,
                          std::string_view port,
                          SslContextPtr ssl_ctx) override;

  void OnResponseHeader(const HttpResponseHeader& header, std::string_view host,
                        std::string_view target) override;

 private:
  std::shared_ptr<RequestAssembler> inner_;
  ProxyConfig proxy_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_REQUEST_ASSEMBLER_H_
