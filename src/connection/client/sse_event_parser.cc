/**
 * @file sse_event_parser.cc
 * @brief Implementation of SSE chunk parser.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/client/sse_event_parser.h"

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

namespace bsrvcore {

namespace {

inline std::string_view TrimLeadingSingleSpace(std::string_view sv) {
  if (!sv.empty() && sv.front() == ' ') {
    sv.remove_prefix(1);
  }
  return sv;
}

}  // namespace

SseEventParser::CompatEventList SseEventParser::Feed(std::string_view chunk) {
  return detail::ToStdVector(FeedAllocated(chunk));
}

SseEventParser::AllocatedEventList SseEventParser::FeedAllocated(
    std::string_view chunk) {
  // The transport may split data arbitrarily; we keep a pending buffer so that
  // incomplete lines at the end of the chunk can be completed by the next
  // Feed().
  pending_.append(chunk.data(), chunk.size());

  AllocatedEventList events;
  std::size_t line_start = 0;

  while (line_start < pending_.size()) {
    // SSE is line-oriented. We only process complete lines (ending with '\n').
    std::size_t const line_end = pending_.find('\n', line_start);
    if (line_end == std::string::npos) {
      break;
    }

    std::string_view line{pending_.data() + line_start, line_end - line_start};
    // Normalize CRLF -> LF by stripping a trailing '\r'.
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    ConsumeLine(line, events);
    line_start = line_end + 1;
  }

  if (line_start > 0) {
    // Remove consumed complete lines; keep the last incomplete line (if any).
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
                                 AllocatedEventList& out) {
  // Call chain: FeedAllocated → ConsumeLine
  //   → EmitEventIfReady (blank line → flush current event)
  //   → ApplySseField    (field:value → update current event)
  if (line.empty()) {
    EmitEventIfReady(out);
    return;
  }

  // Comment line per SSE spec (ignored, no state changes).
  if (line.front() == ':') {
    return;
  }

  // Parse "field:value" (value may be empty). A single leading space in value
  // is trimmed.
  std::size_t const colon = line.find(':');
  std::string_view const field = line.substr(0, colon);
  std::string_view value{};
  if (colon != std::string_view::npos) {
    value = TrimLeadingSingleSpace(line.substr(colon + 1));
  }

  ApplySseField(field, value);
}

void SseEventParser::EmitEventIfReady(AllocatedEventList& out) {
  // A blank line terminates an event. Only emit when we have seen at least
  // one field.
  if (has_field_) {
    out.push_back(current_);
    current_ = SseEvent{};
    has_field_ = false;
  }
}

void SseEventParser::ApplySseField(std::string_view field,
                                   std::string_view value) {
  if (field == "data") {
    // Multiple data lines are concatenated with '\n'.
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
    // retry: must be a non-negative integer (milliseconds).
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
