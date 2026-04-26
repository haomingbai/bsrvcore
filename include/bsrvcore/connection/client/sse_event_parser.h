/**
 * @file sse_event_parser.h
 * @brief Utility parser for text/event-stream chunks.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_SSE_EVENT_PARSER_H_
#define BSRVCORE_CONNECTION_CLIENT_SSE_EVENT_PARSER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore {

/**
 * @brief Parsed SSE event object.
 */
struct SseEvent : public CopyableMovable<SseEvent> {
  /** @brief Event id field (`id:`). */
  std::string id;
  /** @brief Event type field (`event:`). */
  std::string event;
  /** @brief Event payload field (`data:`), joined by '\n' for multiline data.
   */
  std::string data;
  /** @brief Retry field (`retry:`) in milliseconds when provided and valid. */
  std::optional<int> retry_ms;
};

/**
 * @brief Incremental parser for `text/event-stream` chunks.
 *
 * Feed() accepts arbitrary chunk boundaries and returns zero or more fully
 * parsed events. Incomplete lines are buffered across calls.
 */
class SseEventParser : public NonCopyableNonMovable<SseEventParser> {
 public:
  /** @brief Backward-compatible event list type (`std::vector`). */
  using CompatEventList = std::vector<SseEvent>;
  /** @brief Allocator-backed event list type for hot-path callers. */
  using AllocatedEventList = AllocatedVector<SseEvent>;

  /**
   * @brief Feed raw bytes and collect parsed SSE events (compatibility API).
   * @param chunk Raw bytes from transport.
   * @return Zero or more parsed events.
   */
  CompatEventList Feed(std::string_view chunk);

  /**
   * @brief Feed raw bytes and collect allocator-backed SSE events.
   * @param chunk Raw bytes from transport.
   * @return Zero or more parsed events.
   */
  AllocatedEventList FeedAllocated(std::string_view chunk);

  /**
   * @brief Reset internal parser state.
   */
  void Reset();

 private:
  /**
   * @brief Process one complete SSE line.
   *
   * Call chain: FeedAllocated → ConsumeLine
   *   → EmitEventIfReady (blank line → flush current event)
   *   → ApplySseField    (field:value → update current event)
   */
  void ConsumeLine(std::string_view line, AllocatedEventList& out);

  /** @brief Flush current event to output if any field was seen. */
  void EmitEventIfReady(AllocatedEventList& out);

  /** @brief Update current event based on parsed field name and value. */
  void ApplySseField(std::string_view field, std::string_view value);

  std::string pending_;
  SseEvent current_{};
  bool has_field_{false};
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_SSE_EVENT_PARSER_H_
