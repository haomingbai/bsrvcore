/**
 * @file http_route_target_validator.cc
 * @brief Internal validator for parametric route targets.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "http_route_target_validator.h"

#include <boost/regex.hpp>
#include <string>
#include <string_view>

namespace bsrvcore {

namespace route_internal {

bool IsValidParametricTarget(const std::string_view target) {
  // Basic check.
  if (target.empty() || target.length() > 2048 || target[0] != '/') {
    return false;
  }

  // Regex: allow braces as parameter.
  // There can be only one layer of parameter.
  static const boost::regex valid_target_regex(
      R"(^/([a-zA-Z0-9\-._~!$&'()*+,;=:@/?%#\[\]]|\{[a-zA-Z0-9_\-]*\})*$)",
      boost::regex::ECMAScript);

  if (!boost::regex_match(target.begin(), target.end(), valid_target_regex)) {
    return false;
  }

  // Extra check: Check whether the braces pair.
  int brace_count = 0;
  for (char c : target) {
    if (c == '{') {
      brace_count++;
    } else if (c == '}') {
      brace_count--;
      if (brace_count < 0) {
        return false;  // Right more than left.
      }
    }
  }
  if (brace_count != 0) {
    return false;  // Cannot pair.
  }

  // Check the parameter on the path.
  std::string non_param_target;
  bool in_brace = false;
  for (char c : target) {
    if (c == '{') {
      in_brace = true;
    } else if (c == '}') {
      in_brace = false;
    } else if (!in_brace) {
      non_param_target += c;
    }
  }

  if (non_param_target.find("..") != std::string::npos) {
    return false;
  }

  return true;
}

}  // namespace route_internal

}  // namespace bsrvcore
