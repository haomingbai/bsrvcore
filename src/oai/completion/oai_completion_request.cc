/**
 * @file oai_completion_request.cc
 * @brief Request-building helpers for OAI completion facade.
 */

#include <algorithm>
#include <array>
#include <boost/json.hpp>
#include <chrono>
#include <string_view>

#include "oai_completion_detail.h"

namespace bsrvcore::oai::completion::detail {

namespace json = boost::json;

namespace {

std::int64_t NowUnixMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void BuildRequestToolCallObject(const OaiToolCall& call,
                                json::object* call_obj_out) {
  if (call_obj_out == nullptr) {
    return;
  }

  json::object call_obj;
  call_obj["id"] = call.id;
  call_obj["type"] = "function";

  json::object function_obj;
  function_obj["name"] = call.name;
  function_obj["arguments"] = call.arguments;

  call_obj["function"] = std::move(function_obj);
  *call_obj_out = std::move(call_obj);
}

void BuildRequestToolCallsArray(const std::vector<OaiToolCall>& calls,
                                json::array* tool_calls_out) {
  if (tool_calls_out == nullptr) {
    return;
  }

  tool_calls_out->reserve(calls.size());
  for (const auto& call : calls) {
    json::object call_obj;
    BuildRequestToolCallObject(call, &call_obj);
    tool_calls_out->push_back(std::move(call_obj));
  }
}

void BuildRequestMessageObject(const OaiMessage& message,
                               json::object* msg_out) {
  if (msg_out == nullptr) {
    return;
  }

  json::object msg;
  msg["role"] = message.role;
  msg["content"] = message.message;

  if (!message.tool_calls.empty()) {
    json::array tool_calls;
    BuildRequestToolCallsArray(message.tool_calls, &tool_calls);
    msg["tool_calls"] = std::move(tool_calls);
  }

  *msg_out = std::move(msg);
}

void BuildRequestMessagesArray(const std::vector<OaiMessage>& messages,
                               json::array* message_array_out) {
  if (message_array_out == nullptr) {
    return;
  }

  message_array_out->reserve(messages.size());
  for (const auto& message : messages) {
    json::object msg;
    BuildRequestMessageObject(message, &msg);
    message_array_out->push_back(std::move(msg));
  }
}

void BuildRequestToolDefinitionObject(const OaiToolDefinition& tool,
                                      json::object* tool_obj_out) {
  if (tool_obj_out == nullptr) {
    return;
  }

  json::object function_obj;
  function_obj["name"] = tool.name;
  function_obj["description"] = tool.description;
  function_obj["parameters"] = tool.parameters;

  json::object tool_obj;
  tool_obj["type"] = "function";
  tool_obj["function"] = std::move(function_obj);
  *tool_obj_out = std::move(tool_obj);
}

void BuildRequestToolsArray(const std::vector<OaiToolDefinition>& tools,
                            json::array* tool_array_out) {
  if (tool_array_out == nullptr) {
    return;
  }

  tool_array_out->reserve(tools.size());
  for (const auto& tool : tools) {
    json::object tool_obj;
    BuildRequestToolDefinitionObject(tool, &tool_obj);
    tool_array_out->push_back(std::move(tool_obj));
  }
}

bool IsReservedRequestField(std::string_view key) {
  static constexpr std::array<std::string_view, 4> kReservedFields = {
      "model", "stream", "messages", "tools"};
  for (const auto& reserved : kReservedFields) {
    if (reserved == key) {
      return true;
    }
  }
  return false;
}

void MergeModelParamsIntoRoot(const json::object& params, json::object* root) {
  if (root == nullptr) {
    return;
  }

  for (const auto& [key, value] : params) {
    const std::string_view key_view(key.data(), key.size());
    if (IsReservedRequestField(key_view)) {
      continue;
    }
    (*root)[key] = value;
  }
}

}  // namespace

std::string BuildCompletionsUrl(std::string base_url) {
  while (!base_url.empty() && base_url.back() == '/') {
    base_url.pop_back();
  }
  return base_url + "/chat/completions";
}

bool IsHttpSuccessStatus(int status) { return status >= 200 && status < 300; }

std::vector<OaiMessage> CollectMessageChain(
    const std::shared_ptr<OaiCompletionState>& state) {
  std::vector<OaiMessage> reversed;
  std::shared_ptr<const OaiCompletionState> curr = state;
  while (curr) {
    reversed.push_back(curr->GetMessage());
    curr = curr->GetPreviousState();
  }
  std::reverse(reversed.begin(), reversed.end());
  return reversed;
}

bool BuildRequestPayload(const std::vector<OaiMessage>& messages,
                         const std::vector<OaiToolDefinition>& tools,
                         const OaiModelInfo& model_info, bool stream,
                         std::string* body_out, std::string* error_out) {
  if (model_info.model.empty()) {
    if (error_out != nullptr) {
      *error_out = "model_info.model is required";
    }
    return false;
  }

  json::object root;
  MergeModelParamsIntoRoot(model_info.params, &root);
  root["model"] = model_info.model;
  root["stream"] = stream;

  json::array message_array;
  BuildRequestMessagesArray(messages, &message_array);
  root["messages"] = std::move(message_array);

  if (!tools.empty()) {
    json::array tool_array;
    BuildRequestToolsArray(tools, &tool_array);
    root["tools"] = std::move(tool_array);
  }

  if (body_out != nullptr) {
    *body_out = json::serialize(root);
  }
  return true;
}

OaiRequestLog BuildLogSkeleton(const OaiModelInfo& model_info, bool is_stream) {
  OaiRequestLog log;
  log.status = OaiCompletionStatus::kFail;
  log.model = model_info.model;
  log.is_stream = is_stream;
  log.timestamp = NowUnixMs();
  return log;
}

}  // namespace bsrvcore::oai::completion::detail
