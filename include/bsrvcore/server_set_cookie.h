/**
 * @file server_set_cookie.h
 * @brief HTTP Set-Cookie header builder with fluent interface
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-02
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides a fluent interface for building HTTP Set-Cookie headers with
 * support for all standard cookie attributes including security flags,
 * expiration, and SameSite policies.
 */

#pragma once

#ifndef BSRVCORE_SERVER_SET_COOKIE_H_
#define BSRVCORE_SERVER_SET_COOKIE_H_

#include <cstdint>
#include <optional>
#include <string>

#include "bsrvcore/trait.h"

namespace bsrvcore {

/**
 * @brief SameSite attribute values for cookie security
 *
 * Controls when cookies are sent with cross-site requests to prevent CSRF
 * attacks.
 */
enum class SameSite : uint8_t {
  kStrict,  ///< Cookie sent only in same-site context (strictest)
  kLax,     ///< Cookie sent with same-site and top-level navigation requests
            ///< (balanced)
  kNone     ///< Cookie sent with all requests (requires Secure flag)
};

/**
 * @brief Fluent builder for HTTP Set-Cookie headers
 *
 * Provides a type-safe, fluent interface for constructing Set-Cookie headers
 * with all standard attributes. Supports method chaining for concise syntax.
 *
 * @code
 * // Example usage in request handler
 * void Service(std::shared_ptr<HttpServerTask> task) override {
 *   ServerSetCookie cookie;
 *   cookie.SetName("session_id")
 *         .SetValue("abc123def456")
 *         .SetMaxAge(3600)        // 1 hour
 *         .SetPath("/")
 *         .SetHttpOnly(true)
 *         .SetSecure(true)
 *         .SetSameSite(SameSite::kStrict);
 *
 *   task->SetField("Set-Cookie", cookie.ToString());
 *   task->SetResponse(200, "Login successful");
 * }
 *
 * // Example with expiration date
 * ServerSetCookie cookie;
 * cookie.SetName("preferences")
 *       .SetValue("theme=dark")
 *       .SetExpires("Fri, 31 Dec 2025 23:59:59 GMT")
 *       .SetPath("/")
 *       .SetHttpOnly(false);
 * @endcode
 */
class ServerSetCookie : public CopyableMovable<ServerSetCookie> {
 public:
  /**
   * @brief Set the cookie name
   * @param name Cookie name (should not contain spaces or special characters)
   * @return Reference to self for method chaining
   */
  ServerSetCookie &SetName(std::string name);

  /**
   * @brief Set the cookie value
   * @param value Cookie value (will be URL-encoded if necessary)
   * @return Reference to self for method chaining
   */
  ServerSetCookie &SetValue(std::string value);

  /**
   * @brief Set cookie expiration using HTTP date format
   * @param expiry Expiration date in HTTP format (e.g., "Fri, 31 Dec 2025
   * 23:59:59 GMT")
   * @return Reference to self for method chaining
   *
   * @note Mutually exclusive with Max-Age. If both are set, Max-Age takes
   * precedence.
   */
  ServerSetCookie &SetExpires(std::string expiry);

  /**
   * @brief Set cookie lifetime in seconds
   * @param max_age Maximum age in seconds (0 = delete immediately)
   * @return Reference to self for method chaining
   *
   * @note Mutually exclusive with Expires. If both are set, Max-Age takes
   * precedence.
   */
  ServerSetCookie &SetMaxAge(int64_t max_age);

  /**
   * @brief Set the path scope for the cookie
   * @param path URL path prefix (e.g., "/api" makes cookie available to /api/ *)
   * @return Reference to self for method chaining
   */
  ServerSetCookie &SetPath(std::string path);

  /**
   * @brief Set the domain scope for the cookie
   * @param domain Domain name (e.g., "example.com" or ".example.com" for
   * subdomains)
   * @return Reference to self for method chaining
   */
  ServerSetCookie &SetDomain(std::string domain);

  /**
   * @brief Set the SameSite attribute for CSRF protection
   * @param same_site SameSite policy value
   * @return Reference to self for method chaining
   *
   * @note SameSite=None requires Secure flag to be set
   */
  ServerSetCookie &SetSameSite(SameSite same_site);

  /**
   * @brief Set the Secure flag for HTTPS-only transmission
   * @param secure true to restrict cookie to HTTPS connections
   * @return Reference to self for method chaining
   */
  ServerSetCookie &SetSecure(bool secure);

  /**
   * @brief Set the HttpOnly flag for JavaScript protection
   * @param http_only true to prevent JavaScript access to cookie
   * @return Reference to self for method chaining
   */
  ServerSetCookie &SetHttpOnly(bool http_only);

  /**
   * @brief Generate the Set-Cookie header value string
   * @return Formatted Set-Cookie header value
   *
   * @throws std::runtime_error if required fields (name, value) are missing
   *
   * @code
   * // Output example:
   * // "session_id=abc123; Max-Age=3600; Path=/; Secure; HttpOnly;
   * SameSite=Strict"
   * @endcode
   */
  std::string ToString() const;

  /**
   * @brief Construct an empty cookie builder
   */
  ServerSetCookie() = default;

  /**
   * @brief Default destructor
   */
  ~ServerSetCookie() = default;

 private:
  std::optional<std::string> name_;    ///< Cookie name (required)
  std::optional<std::string> value_;   ///< Cookie value (required)
  std::optional<std::string> expiry_;  ///< Expires attribute (HTTP date format)
  std::optional<std::string> path_;    ///< Path attribute
  std::optional<std::string> domain_;  ///< Domain attribute

  std::optional<int64_t> max_age_;     ///< Max-Age attribute (seconds)
  std::optional<SameSite> same_site_;  ///< SameSite attribute
  std::optional<bool> secure_;         ///< Secure flag
  std::optional<bool> http_only_;      ///< HttpOnly flag
};

}  // namespace bsrvcore

#endif
