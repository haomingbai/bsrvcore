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

#include "bsrvcore/json.h"

namespace bsrvcore::oai::completion {

/**
 * @brief Provider connection information for chat completions.
 */
struct OaiCompletionInfo {
  std::string api_key;
  std::string base_url;
  std::optional<std::string> organization;
  std::optional<std::string> project;
};

/**
 * @brief Model selection and model-specific request parameters.
 */
struct OaiModelInfo {
  std::string model;
  JsonObject params;
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
  std::string id;
  std::string name;
  JsonValue arguments;
};

/**
 * @brief Tool definition included in request payload (`tools`).
 */
struct OaiToolDefinition {
  std::string name;
  std::string description;
  JsonObject parameters;
};

/**
 * @brief Chat message stored in state chain.
 */
struct OaiMessage {
  std::string role;
  std::string message;
  std::vector<OaiToolCall> tool_calls;
  std::string reasoning;
};

/**
 * @brief Minimal request/response log for one completion call.
 */
struct OaiRequestLog {
  OaiCompletionStatus status{OaiCompletionStatus::kLocal};
  int http_status_code{0};
  std::string error_message;
  std::string request_id;
  std::string finish_reason;
  std::string model;
  bool is_stream{false};
  std::size_t delta_count{0};
  std::int64_t timestamp{0};
};

/**
 * @brief Immutable linked state node for completion conversations.
 */
class OaiCompletionState {
 public:
  OaiCompletionState(std::shared_ptr<OaiCompletionInfo> info,
                     std::shared_ptr<OaiModelInfo> model_info,
                     OaiMessage message, OaiRequestLog log,
                     std::shared_ptr<OaiCompletionState> previous);

  [[nodiscard]] std::shared_ptr<const OaiCompletionInfo> GetInfo() const;
  [[nodiscard]] std::shared_ptr<const OaiModelInfo> GetModelInfo() const;
  [[nodiscard]] const OaiMessage& GetMessage() const;
  [[nodiscard]] const OaiRequestLog& GetLog() const;
  [[nodiscard]] std::shared_ptr<const OaiCompletionState> GetPreviousState()
      const;

 private:
  const std::shared_ptr<OaiCompletionInfo> info_;
  const std::shared_ptr<OaiModelInfo> model_info_;
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

  [[nodiscard]] StatePtr AppendMessage(const OaiMessage& msg,
                                       StatePtr prev) const;

  [[nodiscard]] bool FetchCompletion(StatePtr state,
                                     std::shared_ptr<OaiModelInfo> model_info,
                                     CompletionCallback cb) const;

  [[nodiscard]] bool FetchCompletion(
      StatePtr state, const std::vector<OaiToolDefinition>& tools,
      const std::shared_ptr<OaiModelInfo>& model_info,
      CompletionCallback cb) const;

  [[nodiscard]] bool FetchStreamCompletion(
      StatePtr state, std::shared_ptr<OaiModelInfo> model_info,
      StreamDoneCallback on_done, StreamDeltaCallback on_delta) const;

  [[nodiscard]] bool FetchStreamCompletion(
      StatePtr state, std::shared_ptr<OaiModelInfo> model_info,
      StreamDoneCallback on_done, StreamDeltaCallback on_delta,
      StreamDeltaCallback on_reasoning_delta) const;

  [[nodiscard]] bool FetchStreamCompletion(
      StatePtr state, const std::vector<OaiToolDefinition>& tools,
      std::shared_ptr<OaiModelInfo> model_info, StreamDoneCallback on_done,
      StreamDeltaCallback on_delta) const;

  [[nodiscard]] bool FetchStreamCompletion(
      StatePtr state, const std::vector<OaiToolDefinition>& tools,
      const std::shared_ptr<OaiModelInfo>& model_info,
      StreamDoneCallback on_done, StreamDeltaCallback on_delta,
      StreamDeltaCallback on_reasoning_delta) const;

 private:
  boost::asio::io_context::executor_type executor_;
  std::shared_ptr<OaiCompletionInfo> info_;
  std::shared_ptr<boost::asio::ssl::context> ssl_ctx_;
};

}  // namespace bsrvcore::oai::completion

#endif  // BSRVCORE_OAI_COMPLETION_OAI_COMPLETION_H_
