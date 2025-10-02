/**
 * @file server_set_cookie.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-02
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/server_set_cookie.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <utility>

using bsrvcore::SameSite;
using bsrvcore::ServerSetCookie;

ServerSetCookie &ServerSetCookie::SetName(std::string name) {
  name_ = std::move(name);
  return *this;
}

ServerSetCookie &ServerSetCookie::SetValue(std::string value) {
  value_ = std::move(value);
  return *this;
}

ServerSetCookie &ServerSetCookie::SetExpires(std::string expiry) {
  expiry_ = std::move(expiry);
  return *this;
}

ServerSetCookie &ServerSetCookie::SetMaxAge(int64_t max_age) {
  max_age_ = max_age;
  return *this;
}

ServerSetCookie &ServerSetCookie::SetPath(std::string path) {
  path_ = std::move(path);
  return *this;
}

ServerSetCookie &ServerSetCookie::SetDomain(std::string domain) {
  domain_ = std::move(domain);
  return *this;
}

ServerSetCookie &ServerSetCookie::SetSameSite(SameSite same_site) {
  same_site_ = same_site;
  return *this;
}

ServerSetCookie &ServerSetCookie::SetSecure(bool secure) {
  secure_ = secure;
  return *this;
}

ServerSetCookie &ServerSetCookie::SetHttpOnly(bool http_only) {
  http_only_ = http_only;
  return *this;
}

std::string ServerSetCookie::ToString() const {
  if (!name_.has_value() || !value_.has_value()) {
    return "";
  }

  if (name_.value().empty() || value_.value().empty()) {
    return "";
  }

  std::string result = std::move(name_.value()) + std::move(value_.value());
  result.push_back(';');

  if (expiry_.has_value() && !expiry_.value().empty()) {
    result.push_back(' ');
    result.append("Expires=");
    result.append(std::move(expiry_.value()));
    result.push_back(';');
  }

  if (path_.has_value() && !path_.value().empty()) {
    result.push_back(' ');
    result.append("Path=");
    result.append(std::move(path_.value()));
    result.push_back(';');
  }

  if (domain_.has_value() && !domain_.value().empty()) {
    result.push_back(' ');
    result.append("Domain=");
    result.append(std::move(domain_.value()));
    result.push_back(';');
  }

  if (max_age_.has_value()) {
    result.push_back(' ');
    result.append("Max-Age=");
    result.append(std::to_string(max_age_.value()));
    result.push_back(';');
  }

  if (same_site_.has_value()) {
    result.push_back(' ');
    result.append("SameSite=");

    switch (same_site_.value()) {
      case SameSite::kStrict: {
        result.append("Strict");
        break;
      }
      case SameSite::kLax: {
        result.append("Lax");
        break;
      }
      case SameSite::kNone: {
        result.append("None");
        break;
      }
      default: {
        result.append("Strict");
        break;
      }
    }
    result.push_back(';');
  }

  if (same_site_.has_value() && same_site_.value() == SameSite::kNone) {
    result.append(" Secure;");
  } else if (secure_.has_value() && secure_.value()) {
    result.append(" Secure;");
  }

  if (http_only_.has_value() && http_only_.value()) {
    result.append(" HttpOnly;");
  }

  while (std::isspace(result.back()) || result.back() == ';') {
    result.pop_back();
  }

  return result;
}
