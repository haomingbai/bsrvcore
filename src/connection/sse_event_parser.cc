/**
 * @file sse_event_parser.cc
 * @brief Implementation of SSE chunk parser.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/sse_event_parser.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <utility>

namespace bsrvcore {

namespace {

inline std::string_view TrimLeadingSingleSpace(std::string_view sv) {
  if (!sv.empty() && sv.front() == ' ') {
    sv.remove_prefix(1);
  }
  return sv;
}

}  // namespace

std::vector<SseEvent> SseEventParser::Feed(std::string_view chunk) {
  pending_.append(chunk.data(), chunk.size());

  std::vector<SseEvent> events;
  std::size_t line_start = 0;

  while (line_start < pending_.size()) {
    std::size_t line_end = pending_.find('\n', line_start);
    if (line_end == std::string::npos) {
      break;
    }

    std::string_view line{pending_.data() + line_start, line_end - line_start};
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    ConsumeLine(line, events);
    line_start = line_end + 1;
  }

  if (line_start > 0) {
    pending_.erase(0, line_start);
  }

  return events;
}

void SseEventParser::Reset() {
  pending_.clear();
  current_ = SseEvent{};
  has_field_ = false;
}

void SseEventParser::ConsumeLine(std::string_view line,
                                 std::vector<SseEvent>& out) {
  if (line.empty()) {
    if (has_field_) {
      out.push_back(current_);
      current_ = SseEvent{};
      has_field_ = false;
    }
    return;
  }

  if (line.front() == ':') {
    return;
  }

  std::size_t colon = line.find(':');
  std::string_view field = line.substr(0, colon);
  std::string_view value{};
  if (colon != std::string_view::npos) {
    value = TrimLeadingSingleSpace(line.substr(colon + 1));
  }

  if (field == "data") {
    if (!current_.data.empty()) {
      current_.data.push_back('\n');
    }
    current_.data.append(value.data(), value.size());
    has_field_ = true;
    return;
  }

  if (field == "id") {
    current_.id.assign(value.data(), value.size());
    has_field_ = true;
    return;
  }

  if (field == "event") {
    current_.event.assign(value.data(), value.size());
    has_field_ = true;
    return;
  }

  if (field == "retry") {
    int retry = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    auto conv = std::from_chars(begin, end, retry);
    if (conv.ec == std::errc{} && conv.ptr == end && retry >= 0) {
      current_.retry_ms = retry;
      has_field_ = true;
    }
  }
}

}  // namespace bsrvcore
