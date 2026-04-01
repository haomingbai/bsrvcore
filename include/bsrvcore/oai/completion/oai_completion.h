/**
 * @file oai_completion.h
 * @brief Chat-style OAI completion facade (DeepSeek/OpenAI compatible).
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-01
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_OAI_COMPLETION_OAI_COMPLETION_H_
#define BSRVCORE_OAI_COMPLETION_OAI_COMPLETION_H_

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bsrvcore::oai::completion {

/**
 * @brief Provider connection information for chat completions.
 */
struct OaiCompletionInfo {
  std::string api_key{};
  std::string base_url{};
  std::string model{};
  std::optional<std::string> organization{};
  std::optional<std::string> project{};
};

/**
 * @brief Lifecycle status for one completion request.
 */
enum class OaiCompletionStatus {
  kLocal,
  kSuccess,
  kFail,
};

/**
 * @brief Tool call object parsed from assistant responses.
 */
struct OaiToolCall {
  std::string id{};
  std::string name{};
  std::string arguments_json{};
};

/**
 * @brief Tool definition included in request payload (`tools`).
 */
struct OaiToolDefinition {
  std::string name{};
  std::string description{};
  std::string parameters_json{"{}"};
};

/**
 * @brief Chat message stored in state chain.
 */
struct OaiMessage {
  std::string role{};
  std::string message{};
  std::vector<OaiToolCall> tool_calls{};
  std::string reasoning{};
};

/**
 * @brief Minimal request/response log for one completion call.
 */
struct OaiRequestLog {
  OaiCompletionStatus status{OaiCompletionStatus::kLocal};
  int http_status_code{0};
  std::string error_message{};
  std::string request_id{};
  std::string finish_reason{};
  std::string model{};
  bool is_stream{false};
  std::size_t delta_count{0};
  std::int64_t timestamp{0};
};

/**
 * @brief Immutable linked state node for completion conversations.
 */
class OaiCompletionState {
 public:
  OaiCompletionState(std::shared_ptr<OaiCompletionInfo> info, OaiMessage message,
                     OaiRequestLog log,
                     std::shared_ptr<OaiCompletionState> previous);

  std::shared_ptr<const OaiCompletionInfo> GetInfo() const;
  const OaiMessage& GetMessage() const;
  const OaiRequestLog& GetLog() const;
  std::shared_ptr<const OaiCompletionState> GetPreviousState() const;

 private:
  const std::shared_ptr<OaiCompletionInfo> info_;
  const OaiMessage message_;
  const OaiRequestLog log_;
  const std::shared_ptr<OaiCompletionState> previous_;
};

/**
 * @brief Factory of immutable completion states and asynchronous fetch calls.
 */
class OaiCompletionFactory {
 public:
  using StatePtr = std::shared_ptr<OaiCompletionState>;
  using CompletionCallback = std::function<void(StatePtr)>;
  using StreamDoneCallback = std::function<void(StatePtr)>;
  using StreamDeltaCallback = std::function<void(const std::string&)>;

  OaiCompletionFactory(boost::asio::io_context::executor_type executor,
                       std::shared_ptr<OaiCompletionInfo> info);

  StatePtr AppendMessage(const OaiMessage& msg, StatePtr prev) const;

  bool FetchCompletion(StatePtr state, CompletionCallback cb) const;

  bool FetchCompletion(StatePtr state, const std::vector<OaiToolDefinition>& tools,
                       CompletionCallback cb) const;

  bool FetchStreamCompletion(StatePtr state, StreamDoneCallback on_done,
                             StreamDeltaCallback on_delta) const;

  bool FetchStreamCompletion(StatePtr state, StreamDoneCallback on_done,
                             StreamDeltaCallback on_delta,
                             StreamDeltaCallback on_reasoning_delta) const;

  bool FetchStreamCompletion(StatePtr state,
                             const std::vector<OaiToolDefinition>& tools,
                             StreamDoneCallback on_done,
                             StreamDeltaCallback on_delta) const;

  bool FetchStreamCompletion(StatePtr state,
                             const std::vector<OaiToolDefinition>& tools,
                             StreamDoneCallback on_done,
                             StreamDeltaCallback on_delta,
                             StreamDeltaCallback on_reasoning_delta) const;

 private:
  boost::asio::io_context::executor_type executor_;
  std::shared_ptr<OaiCompletionInfo> info_;
  std::shared_ptr<boost::asio::ssl::context> ssl_ctx_;
};

}  // namespace bsrvcore::oai::completion

#endif  // BSRVCORE_OAI_COMPLETION_OAI_COMPLETION_H_
