/**
 * @file http_url_parser.cc
 * @brief Internal helpers for parsing http/https absolute URLs.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "http_url_parser.h"

#include <boost/system/result.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url_view.hpp>
#include <optional>
#include <string>
#include <utility>

namespace bsrvcore::connection_internal {

std::optional<ParsedUrl> ParseHttpUrl(const std::string& url) {
  // Parse absolute URI like: http(s)://host[:port]/path?query.
  // We intentionally accept only http/https schemes here.
  auto parsed = boost::urls::parse_uri(url);
  if (!parsed) {
    return std::nullopt;
  }

  const auto& u = parsed.value();
  const auto scheme = u.scheme();
  if (scheme != "http" && scheme != "https") {
    return std::nullopt;
  }

  if (u.host().empty()) {
    return std::nullopt;
  }

  ParsedUrl out;
  out.https = (scheme == "https");
  out.host = std::string(u.host());
  // If port is absent, pick the scheme default.
  out.port = u.has_port() ? std::string(u.port()) : (out.https ? "443" : "80");

  // Rebuild request-target (path + optional query). Keep encoded bytes as-is.
  std::string target = std::string(u.encoded_path());
  if (target.empty()) {
    target = "/";
  }
  if (u.has_query()) {
    target.push_back('?');
    target.append(u.encoded_query());
  }
  out.target = std::move(target);

  return out;
}

}  // namespace bsrvcore::connection_internal
