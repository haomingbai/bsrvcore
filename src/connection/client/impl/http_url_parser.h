/**
 * @file http_url_parser.h
 * @brief Internal helpers for parsing http/https absolute URLs.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_IMPL_HTTP_URL_PARSER_H_
#define BSRVCORE_CONNECTION_CLIENT_IMPL_HTTP_URL_PARSER_H_

#include <optional>
#include <string>

namespace bsrvcore::connection_internal {

struct ParsedUrl {
  bool https{false};
  std::string host;
  std::string port;
  std::string target;
};

// Parse absolute URI like: http(s)://host[:port]/path?query.
//
// - Only accepts "http" and "https" schemes.
// - If port is absent, uses 80/443 by scheme.
// - Rebuilds request-target as encoded_path + optional '?' + encoded_query.
// - Returns std::nullopt on parse failure or missing host.
std::optional<ParsedUrl> ParseHttpUrl(const std::string& url);

}  // namespace bsrvcore::connection_internal

#endif  // BSRVCORE_CONNECTION_CLIENT_IMPL_HTTP_URL_PARSER_H_
