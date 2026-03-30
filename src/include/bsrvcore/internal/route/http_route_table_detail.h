/**
 * @file http_route_table_detail.h
 * @brief Internal helpers shared by route-table implementation files.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_ROUTE_HTTP_ROUTE_TABLE_DETAIL_H_
#define BSRVCORE_INTERNAL_ROUTE_HTTP_ROUTE_TABLE_DETAIL_H_

#include <string>
#include <string_view>
#include <vector>

namespace bsrvcore::route_internal::detail {

inline std::string_view StripQuery(std::string_view target) noexcept {
  return target.substr(0, target.find('?'));
}

inline bool IsParameterSegment(std::string_view segment) noexcept {
  return segment.size() >= 2 && segment.front() == '{' && segment.back() == '}';
}

inline std::string_view ExtractParamName(std::string_view segment) noexcept {
  if (!IsParameterSegment(segment)) {
    return {};
  }
  return segment.substr(1, segment.size() - 2);
}

inline std::vector<std::string_view> SplitTargetSegments(std::string_view target) {
  std::vector<std::string_view> segments;
  std::string_view url = StripQuery(target);

  while (!url.empty()) {
    const auto slash = url.find('/');
    auto part = url.substr(0, slash);
    if (!part.empty()) {
      segments.emplace_back(part);
    }

    if (slash == std::string_view::npos) {
      break;
    }
    url.remove_prefix(slash + 1);
  }

  return segments;
}

}  // namespace bsrvcore::route_internal::detail

#endif
