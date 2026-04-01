/**
 * @file oai_completion.cc
 * @brief Implementation of chat-style OAI completion facade.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-01
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/oai/completion/oai_completion.h"

#include <algorithm>
#include <atomic>
#include <boost/asio/post.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/http_sse_client_task.h"
#include "bsrvcore/connection/client/sse_event_parser.h"

namespace bsrvcore::oai::completion {

namespace {

namespace http = boost::beast::http;
namespace json = boost::json;

struct ToolCallAccumulator {
  std::string id;
  std::string name;
  std::string arguments_json;
};

struct StreamAggregate {
  std::atomic<bool> done{false};
  int http_status_code{0};
  std::size_t delta_count{0};
  std::string accumulated_message;
  std::string accumulated_reasoning;
  std::map<std::size_t, ToolCallAccumulator> tool_calls;
  std::string request_id;
  std::string finish_reason;
  std::string model;
  std::string error_message;
};

std::int64_t NowUnixMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

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

std::optional<std::string> JsonStringFromObjectField(const json::object& obj,
                                                     const char* key) {
  const json::value* value = obj.if_contains(key);
  if (value == nullptr || !value->is_string()) {
    return std::nullopt;
  }
  return std::string(value->as_string().c_str());
}

std::vector<OaiToolCall> ParseToolCallsFromArray(const json::array& array) {
  std::vector<OaiToolCall> out;
  out.reserve(array.size());
  for (const auto& item : array) {
    if (!item.is_object()) {
      continue;
    }

    const auto& call_obj = item.as_object();
    OaiToolCall call;

    if (auto id = JsonStringFromObjectField(call_obj, "id")) {
      call.id = std::move(*id);
    }

    const json::value* function_value = call_obj.if_contains("function");
    if (function_value != nullptr && function_value->is_object()) {
      const auto& function_obj = function_value->as_object();
      if (auto name = JsonStringFromObjectField(function_obj, "name")) {
        call.name = std::move(*name);
      }

      const json::value* args_value = function_obj.if_contains("arguments");
      if (args_value != nullptr) {
        if (args_value->is_string()) {
          call.arguments_json = std::string(args_value->as_string().c_str());
        } else {
          call.arguments_json = json::serialize(*args_value);
        }
      }
    }

    if (!call.id.empty() || !call.name.empty() || !call.arguments_json.empty()) {
      out.push_back(std::move(call));
    }
  }
  return out;
}

std::optional<json::value> TryParseJsonValue(const std::string& text) {
  json::error_code parse_ec;
  json::value value = json::parse(text, parse_ec);
  if (parse_ec) {
    return std::nullopt;
  }
  return value;
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

  if (auto args_value = TryParseJsonValue(call.arguments_json)) {
    function_obj["arguments"] = std::move(*args_value);
  } else {
    function_obj["arguments"] = call.arguments_json;
  }

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

bool BuildRequestToolDefinitionObject(const OaiToolDefinition& tool,
                                      json::object* tool_obj_out,
                                      std::string* error_out) {
  if (tool_obj_out == nullptr) {
    return false;
  }

  json::object function_obj;
  function_obj["name"] = tool.name;
  function_obj["description"] = tool.description;

  auto parameters = TryParseJsonValue(tool.parameters_json);
  if (!parameters.has_value()) {
    if (error_out != nullptr) {
      *error_out = "invalid tool parameters_json for tool: " + tool.name;
    }
    return false;
  }
  function_obj["parameters"] = std::move(*parameters);

  json::object tool_obj;
  tool_obj["type"] = "function";
  tool_obj["function"] = std::move(function_obj);
  *tool_obj_out = std::move(tool_obj);
  return true;
}

bool BuildRequestToolsArray(const std::vector<OaiToolDefinition>& tools,
                            json::array* tool_array_out,
                            std::string* error_out) {
  if (tool_array_out == nullptr) {
    return false;
  }

  tool_array_out->reserve(tools.size());
  for (const auto& tool : tools) {
    json::object tool_obj;
    if (!BuildRequestToolDefinitionObject(tool, &tool_obj, error_out)) {
      return false;
    }
    tool_array_out->push_back(std::move(tool_obj));
  }
  return true;
}

bool BuildRequestPayload(const std::vector<OaiMessage>& messages,
                         const std::vector<OaiToolDefinition>& tools,
                         const OaiCompletionInfo& info, bool stream,
                         std::string* body_out, std::string* error_out) {
  json::object root;
  root["model"] = info.model;
  root["stream"] = stream;

  json::array message_array;
  BuildRequestMessagesArray(messages, &message_array);
  root["messages"] = std::move(message_array);

  if (!tools.empty()) {
    json::array tool_array;
    if (!BuildRequestToolsArray(tools, &tool_array, error_out)) {
      return false;
    }
    root["tools"] = std::move(tool_array);
  }

  if (body_out != nullptr) {
    *body_out = json::serialize(root);
  }
  return true;
}

std::string ExtractErrorMessageFromJsonBody(const std::string& body) {
  json::error_code ec;
  json::value root = json::parse(body, ec);
  if (ec || !root.is_object()) {
    return {};
  }

  const auto& root_obj = root.as_object();
  const json::value* error_value = root_obj.if_contains("error");
  if (error_value == nullptr) {
    return {};
  }

  if (error_value->is_string()) {
    return std::string(error_value->as_string().c_str());
  }

  if (!error_value->is_object()) {
    return {};
  }

  const auto& error_obj = error_value->as_object();
  if (auto message = JsonStringFromObjectField(error_obj, "message")) {
    return std::move(*message);
  }
  return {};
}

void ParseCompletionLogFromRootObject(const json::object& root_obj,
                                      OaiRequestLog* log) {
  if (log == nullptr) {
    return;
  }

  if (auto request_id = JsonStringFromObjectField(root_obj, "id")) {
    log->request_id = std::move(*request_id);
  }

  if (auto model = JsonStringFromObjectField(root_obj, "model")) {
    log->model = std::move(*model);
  }
}

void ParseCompletionLogFromChoiceObject(const json::object& choice_obj,
                                        OaiRequestLog* log) {
  if (log == nullptr) {
    return;
  }

  if (auto finish_reason = JsonStringFromObjectField(choice_obj,
                                                     "finish_reason")) {
    log->finish_reason = std::move(*finish_reason);
  }
}

bool ExtractResponseFirstChoiceObject(const json::object& root_obj,
                                      const json::object** choice_out,
                                      std::string* error_out) {
  const json::value* choices_value = root_obj.if_contains("choices");
  if (choices_value == nullptr || !choices_value->is_array() ||
      choices_value->as_array().empty()) {
    if (error_out != nullptr) {
      *error_out = "response choices is missing or empty";
    }
    return false;
  }

  const json::value& first_choice_value = choices_value->as_array().front();
  if (!first_choice_value.is_object()) {
    if (error_out != nullptr) {
      *error_out = "response choice[0] is not object";
    }
    return false;
  }

  if (choice_out != nullptr) {
    *choice_out = &first_choice_value.as_object();
  }
  return true;
}

bool ExtractResponseChoiceMessageObject(const json::object& choice_obj,
                                        const json::object** message_out,
                                        std::string* error_out) {
  const json::value* message_value = choice_obj.if_contains("message");
  if (message_value == nullptr || !message_value->is_object()) {
    if (error_out != nullptr) {
      *error_out = "response choice[0].message is missing";
    }
    return false;
  }

  if (message_out != nullptr) {
    *message_out = &message_value->as_object();
  }
  return true;
}

void ParseMessageTextFieldOrSerialize(const json::object& message_obj,
                                      const char* key,
                                      std::string* out_text) {
  if (out_text == nullptr) {
    return;
  }

  const json::value* value = message_obj.if_contains(key);
  if (value == nullptr) {
    return;
  }

  if (value->is_string()) {
    *out_text = std::string(value->as_string().c_str());
  } else if (!value->is_null()) {
    *out_text = json::serialize(*value);
  }
}

void ParseAssistantMessageFromObject(const json::object& message_obj,
                                     OaiMessage* message_out) {
  if (message_out == nullptr) {
    return;
  }

  OaiMessage out;
  out.role = JsonStringFromObjectField(message_obj, "role").value_or(
      std::string("assistant"));
  ParseMessageTextFieldOrSerialize(message_obj, "content", &out.message);
  ParseMessageTextFieldOrSerialize(message_obj, "reasoning_content",
                                  &out.reasoning);

  const json::value* tool_calls_value = message_obj.if_contains("tool_calls");
  if (tool_calls_value != nullptr && tool_calls_value->is_array()) {
    out.tool_calls = ParseToolCallsFromArray(tool_calls_value->as_array());
  }

  *message_out = std::move(out);
}

bool ParseCompletionResponseBody(const std::string& body, OaiMessage* message_out,
                                 OaiRequestLog* log, std::string* error_out) {
  json::error_code ec;
  json::value root = json::parse(body, ec);
  if (ec) {
    if (error_out != nullptr) {
      *error_out = "invalid JSON response body";
    }
    return false;
  }
  if (!root.is_object()) {
    if (error_out != nullptr) {
      *error_out = "response root is not JSON object";
    }
    return false;
  }

  const auto& root_obj = root.as_object();

  ParseCompletionLogFromRootObject(root_obj, log);

  const json::object* first_choice = nullptr;
  if (!ExtractResponseFirstChoiceObject(root_obj, &first_choice, error_out)) {
    return false;
  }

  ParseCompletionLogFromChoiceObject(*first_choice, log);

  const json::object* message_obj = nullptr;
  if (!ExtractResponseChoiceMessageObject(*first_choice, &message_obj,
                                          error_out)) {
    return false;
  }

  ParseAssistantMessageFromObject(*message_obj, message_out);
  return true;
}

const json::object* TryGetStreamFirstChoiceObject(const json::object& root_obj) {
  const json::value* choices_value = root_obj.if_contains("choices");
  if (choices_value == nullptr || !choices_value->is_array() ||
      choices_value->as_array().empty()) {
    return nullptr;
  }

  const json::value& first_choice_value = choices_value->as_array().front();
  if (!first_choice_value.is_object()) {
    return nullptr;
  }

  return &first_choice_value.as_object();
}

const json::object* TryGetStreamDeltaObject(const json::object& choice_obj) {
  const json::value* delta_value = choice_obj.if_contains("delta");
  if (delta_value == nullptr || !delta_value->is_object()) {
    return nullptr;
  }

  return &delta_value->as_object();
}

void ParseStreamDeltaTextFields(const json::object& delta_obj,
                                std::string* out_delta,
                                std::string* out_reasoning_delta) {
  if (out_delta != nullptr) {
    if (auto content = JsonStringFromObjectField(delta_obj, "content")) {
      *out_delta = std::move(*content);
    }
  }

  if (out_reasoning_delta != nullptr) {
    if (auto reasoning =
            JsonStringFromObjectField(delta_obj, "reasoning_content")) {
      *out_reasoning_delta = std::move(*reasoning);
    }
  }
}

void AccumulateStreamToolCallsFromArray(
    const json::array& tool_calls_array,
    std::map<std::size_t, ToolCallAccumulator>* tool_calls) {
  if (tool_calls == nullptr) {
    return;
  }

  std::size_t fallback_index = 0;
  for (const auto& call_value : tool_calls_array) {
    if (!call_value.is_object()) {
      ++fallback_index;
      continue;
    }

    const auto& call_obj = call_value.as_object();
    std::size_t index = fallback_index;
    const json::value* index_value = call_obj.if_contains("index");
    if (index_value != nullptr && index_value->is_int64() &&
        index_value->as_int64() >= 0) {
      index = static_cast<std::size_t>(index_value->as_int64());
    }
    ++fallback_index;

    auto& acc = (*tool_calls)[index];

    if (auto id = JsonStringFromObjectField(call_obj, "id")) {
      acc.id = std::move(*id);
    }

    const json::value* function_value = call_obj.if_contains("function");
    if (function_value != nullptr && function_value->is_object()) {
      const auto& function_obj = function_value->as_object();
      if (auto name = JsonStringFromObjectField(function_obj, "name")) {
        acc.name = std::move(*name);
      }

      const json::value* args_value = function_obj.if_contains("arguments");
      if (args_value != nullptr) {
        if (args_value->is_string()) {
          acc.arguments_json.append(args_value->as_string().c_str());
        } else {
          acc.arguments_json.append(json::serialize(*args_value));
        }
      }
    }
  }
}

void MaybeAccumulateStreamToolCalls(
    const json::object& delta_obj,
    std::map<std::size_t, ToolCallAccumulator>* tool_calls) {
  const json::value* tool_calls_value = delta_obj.if_contains("tool_calls");
  if (tool_calls_value == nullptr || !tool_calls_value->is_array() ||
      tool_calls == nullptr) {
    return;
  }

  AccumulateStreamToolCallsFromArray(tool_calls_value->as_array(), tool_calls);
}

bool ApplyStreamDeltaPayload(
    const std::string& payload, std::string* out_delta,
    std::string* out_reasoning_delta,
    std::map<std::size_t, ToolCallAccumulator>* tool_calls,
    std::string* request_id, std::string* finish_reason, std::string* model,
    std::string* error_out) {
  json::error_code ec;
  json::value root = json::parse(payload, ec);
  if (ec || !root.is_object()) {
    if (error_out != nullptr) {
      *error_out = "invalid stream delta JSON payload";
    }
    return false;
  }

  const auto& root_obj = root.as_object();
  if (request_id != nullptr && request_id->empty()) {
    if (auto id = JsonStringFromObjectField(root_obj, "id")) {
      *request_id = std::move(*id);
    }
  }

  if (model != nullptr) {
    if (auto parsed_model = JsonStringFromObjectField(root_obj, "model")) {
      *model = std::move(*parsed_model);
    }
  }

  const json::object* choice_obj = TryGetStreamFirstChoiceObject(root_obj);
  if (choice_obj == nullptr) {
    return true;
  }

  if (finish_reason != nullptr) {
    if (auto parsed_finish = JsonStringFromObjectField(*choice_obj,
                                                       "finish_reason")) {
      *finish_reason = std::move(*parsed_finish);
    }
  }

  const json::object* delta_obj = TryGetStreamDeltaObject(*choice_obj);
  if (delta_obj == nullptr) {
    return true;
  }

  ParseStreamDeltaTextFields(*delta_obj, out_delta, out_reasoning_delta);
  MaybeAccumulateStreamToolCalls(*delta_obj, tool_calls);

  return true;
}

std::vector<OaiToolCall> BuildToolCallsFromAccumulation(
    const std::map<std::size_t, ToolCallAccumulator>& tool_calls) {
  std::vector<OaiToolCall> out;
  out.reserve(tool_calls.size());
  for (const auto& [_, acc] : tool_calls) {
    OaiToolCall call;
    call.id = acc.id;
    call.name = acc.name;
    call.arguments_json = acc.arguments_json;
    if (!call.id.empty() || !call.name.empty() || !call.arguments_json.empty()) {
      out.push_back(std::move(call));
    }
  }
  return out;
}

OaiRequestLog BuildLogSkeleton(const OaiCompletionInfo& info, bool is_stream) {
  OaiRequestLog log;
  log.status = OaiCompletionStatus::kFail;
  log.model = info.model;
  log.is_stream = is_stream;
  log.timestamp = NowUnixMs();
  return log;
}

enum class StreamEventAction {
  kContinue,
  kDone,
  kFail,
};

StreamEventAction ProcessStreamEventData(
    const std::string& data, StreamAggregate* agg,
    const std::shared_ptr<OaiCompletionFactory::StreamDeltaCallback>& on_delta,
    const std::shared_ptr<OaiCompletionFactory::StreamDeltaCallback>&
        on_reasoning_delta,
    std::string* error_out) {
  if (agg == nullptr) {
    if (error_out != nullptr) {
      *error_out = "internal error";
    }
    return StreamEventAction::kFail;
  }

  if (data.empty()) {
    return StreamEventAction::kContinue;
  }
  if (data == "[DONE]") {
    return StreamEventAction::kDone;
  }

  ++agg->delta_count;
  std::string delta;
  std::string reasoning_delta;
  std::string parse_error;
  if (!ApplyStreamDeltaPayload(data, &delta, &reasoning_delta, &agg->tool_calls,
                               &agg->request_id, &agg->finish_reason,
                               &agg->model, &parse_error)) {
    if (error_out != nullptr) {
      *error_out = std::move(parse_error);
    }
    return StreamEventAction::kFail;
  }

  if (!reasoning_delta.empty()) {
    agg->accumulated_reasoning.append(reasoning_delta);
    if (on_reasoning_delta != nullptr && (*on_reasoning_delta)) {
      (*on_reasoning_delta)(reasoning_delta);
    }
  }

  if (!delta.empty()) {
    agg->accumulated_message.append(delta);
    if (on_delta != nullptr && (*on_delta)) {
      (*on_delta)(delta);
    }
  }

  return StreamEventAction::kContinue;
}

void PullNextStreamChunk(
    const std::shared_ptr<HttpSseClientTask>& client,
    const std::shared_ptr<SseEventParser>& parser,
    const std::shared_ptr<StreamAggregate>& agg,
    const std::shared_ptr<std::function<void(bool, std::string)>>& finish,
    const std::shared_ptr<OaiCompletionFactory::StreamDeltaCallback>&
        on_delta,
    const std::shared_ptr<OaiCompletionFactory::StreamDeltaCallback>&
        on_reasoning_delta) {
  client->Next(
      [client, parser, agg, finish, on_delta, on_reasoning_delta](
          const HttpSseClientResult& result) mutable {
        if (agg->done.load(std::memory_order_acquire)) {
          return;
        }

        if (result.cancelled) {
          (*finish)(false, "stream cancelled");
          return;
        }

        if (result.ec && !result.eof) {
          (*finish)(false, result.ec.message());
          return;
        }

        if (!result.chunk.empty()) {
          auto events = parser->Feed(result.chunk);
          for (const auto& event : events) {
            std::string parse_error;
            const StreamEventAction action =
                ProcessStreamEventData(event.data, agg.get(), on_delta,
                                       on_reasoning_delta,
                                       &parse_error);
            if (action == StreamEventAction::kDone) {
              (*finish)(true, "");
              return;
            }
            if (action == StreamEventAction::kFail) {
              (*finish)(false, std::move(parse_error));
              return;
            }
          }
        }

        if (result.eof) {
          (*finish)(true, "");
          return;
        }

        PullNextStreamChunk(client, parser, agg, finish, on_delta,
                            on_reasoning_delta);
      });
}

http::verb ToPostVerb() { return http::verb::post; }

}  // namespace

bool OaiCompletionFactory::FetchCompletion(StatePtr state,
                                           CompletionCallback cb) const {
  return FetchCompletion(std::move(state), {}, std::move(cb));
}

bool OaiCompletionFactory::FetchCompletion(
    StatePtr state, const std::vector<OaiToolDefinition>& tools,
    CompletionCallback cb) const {
  if (!state || !cb || !info_ || info_->base_url.empty() || info_->model.empty()) {
    return false;
  }

  std::string request_body;
  std::string build_error;
  if (!BuildRequestPayload(CollectMessageChain(state), tools, *info_, false,
                           &request_body, &build_error)) {
    OaiMessage assistant;
    assistant.role = "assistant";

    OaiRequestLog log = BuildLogSkeleton(*info_, false);
    log.error_message = std::move(build_error);
    auto failure_state =
        AllocateShared<OaiCompletionState>(info_, std::move(assistant),
                                           std::move(log), std::move(state));
    cb(std::move(failure_state));
    return true;
  }

  HttpClientOptions options;
  options.connect_timeout = std::chrono::seconds(10);
  options.read_header_timeout = std::chrono::seconds(10);
  options.read_body_timeout = std::chrono::seconds(60);

  auto task = HttpClientTask::CreateFromUrl(executor_, *ssl_ctx_,
                                            BuildCompletionsUrl(info_->base_url),
                                            ToPostVerb(), options);

  auto& request = task->Request();
  request.set(http::field::content_type, "application/json");
  request.set(http::field::accept, "application/json");
  if (!info_->api_key.empty()) {
    request.set(http::field::authorization, "Bearer " + info_->api_key);
  }
  if (info_->organization.has_value()) {
    request.set("OpenAI-Organization", *info_->organization);
  }
  if (info_->project.has_value()) {
    request.set("OpenAI-Project", *info_->project);
  }
  request.body() = std::move(request_body);

  task->OnDone([state = std::move(state), cb = std::move(cb),
                info = info_](const HttpClientResult& result) mutable {
    OaiMessage assistant;
    assistant.role = "assistant";

    OaiRequestLog log = BuildLogSkeleton(*info, false);
    log.http_status_code = result.response.result_int();

    const auto request_id_header = result.response.base().find("x-request-id");
    if (request_id_header != result.response.base().end()) {
      log.request_id = std::string(request_id_header->value().data(),
                                   request_id_header->value().size());
    }

    if (result.ec) {
      log.error_message = result.ec.message();
      auto failure_state = AllocateShared<OaiCompletionState>(
          info, std::move(assistant), std::move(log), std::move(state));
      cb(std::move(failure_state));
      return;
    }

    if (result.cancelled) {
      log.error_message = "request cancelled";
      auto failure_state = AllocateShared<OaiCompletionState>(
          info, std::move(assistant), std::move(log), std::move(state));
      cb(std::move(failure_state));
      return;
    }

    std::string parse_error;
    if (!IsHttpSuccessStatus(log.http_status_code)) {
      parse_error = ExtractErrorMessageFromJsonBody(result.response.body());
      log.error_message = parse_error.empty()
                              ? ("HTTP status " +
                                 std::to_string(log.http_status_code))
                              : std::move(parse_error);
      auto failure_state = AllocateShared<OaiCompletionState>(
          info, std::move(assistant), std::move(log), std::move(state));
      cb(std::move(failure_state));
      return;
    }

    if (!ParseCompletionResponseBody(result.response.body(), &assistant, &log,
                                     &parse_error)) {
      log.error_message = std::move(parse_error);
      auto failure_state = AllocateShared<OaiCompletionState>(
          info, std::move(assistant), std::move(log), std::move(state));
      cb(std::move(failure_state));
      return;
    }

    log.status = OaiCompletionStatus::kSuccess;
    auto next_state = AllocateShared<OaiCompletionState>(
        info, std::move(assistant), std::move(log), std::move(state));
    cb(std::move(next_state));
  });

  task->Start();
  return true;
}

bool OaiCompletionFactory::FetchStreamCompletion(StatePtr state,
                                                 StreamDoneCallback on_done,
                                                 StreamDeltaCallback on_delta)
    const {
  return FetchStreamCompletion(std::move(state), std::vector<OaiToolDefinition>{},
                               std::move(on_done), std::move(on_delta),
                               StreamDeltaCallback{});
}

bool OaiCompletionFactory::FetchStreamCompletion(
    StatePtr state, StreamDoneCallback on_done, StreamDeltaCallback on_delta,
    StreamDeltaCallback on_reasoning_delta) const {
  return FetchStreamCompletion(std::move(state), std::vector<OaiToolDefinition>{},
                               std::move(on_done), std::move(on_delta),
                               std::move(on_reasoning_delta));
}

bool OaiCompletionFactory::FetchStreamCompletion(
    StatePtr state, const std::vector<OaiToolDefinition>& tools,
    StreamDoneCallback on_done, StreamDeltaCallback on_delta) const {
  return FetchStreamCompletion(std::move(state), tools, std::move(on_done),
                               std::move(on_delta), StreamDeltaCallback{});
}

bool OaiCompletionFactory::FetchStreamCompletion(
    StatePtr state, const std::vector<OaiToolDefinition>& tools,
    StreamDoneCallback on_done, StreamDeltaCallback on_delta,
    StreamDeltaCallback on_reasoning_delta) const {
  if (!state || !on_done || (!on_delta && !on_reasoning_delta) || !info_ ||
      info_->base_url.empty() || info_->model.empty()) {
    return false;
  }

  std::string request_body;
  std::string build_error;
  if (!BuildRequestPayload(CollectMessageChain(state), tools, *info_, true,
                           &request_body, &build_error)) {
    OaiMessage assistant;
    assistant.role = "assistant";

    OaiRequestLog log = BuildLogSkeleton(*info_, true);
    log.error_message = std::move(build_error);
    auto failure_state =
        AllocateShared<OaiCompletionState>(info_, std::move(assistant),
                                           std::move(log), std::move(state));
    on_done(std::move(failure_state));
    return true;
  }

  HttpSseClientOptions options;
  options.connect_timeout = std::chrono::seconds(10);
  options.read_header_timeout = std::chrono::seconds(10);
  options.read_body_timeout = std::chrono::seconds(60);

  auto client = HttpSseClientTask::CreateFromUrl(
      executor_, *ssl_ctx_, BuildCompletionsUrl(info_->base_url), options);

  auto& request = client->Request();
  request.method(http::verb::post);
  request.set(http::field::content_type, "application/json");
  request.set(http::field::accept, "text/event-stream");
  if (!info_->api_key.empty()) {
    request.set(http::field::authorization, "Bearer " + info_->api_key);
  }
  if (info_->organization.has_value()) {
    request.set("OpenAI-Organization", *info_->organization);
  }
  if (info_->project.has_value()) {
    request.set("OpenAI-Project", *info_->project);
  }
  request.body() = std::move(request_body);

  auto parser = AllocateShared<SseEventParser>();
  auto agg = AllocateShared<StreamAggregate>();
    std::shared_ptr<OaiCompletionFactory::StreamDeltaCallback> delta_cb;
    if (on_delta) {
    delta_cb = AllocateShared<OaiCompletionFactory::StreamDeltaCallback>(
      std::move(on_delta));
    }

    std::shared_ptr<OaiCompletionFactory::StreamDeltaCallback>
      reasoning_delta_cb;
    if (on_reasoning_delta) {
    reasoning_delta_cb =
      AllocateShared<OaiCompletionFactory::StreamDeltaCallback>(
        std::move(on_reasoning_delta));
    }

  auto finish = AllocateShared<std::function<void(bool, std::string)>>();
  *finish = [agg, on_done = std::move(on_done), info = info_,
             state = std::move(state)](bool success,
                                       std::string error_message) mutable {
    if (agg->done.exchange(true)) {
      return;
    }

    if (!error_message.empty()) {
      agg->error_message = std::move(error_message);
    }

    OaiMessage assistant;
    assistant.role = "assistant";
    assistant.message = agg->accumulated_message;
    assistant.tool_calls = BuildToolCallsFromAccumulation(agg->tool_calls);
    assistant.reasoning = agg->accumulated_reasoning;

    OaiRequestLog log = BuildLogSkeleton(*info, true);
    log.status = success ? OaiCompletionStatus::kSuccess
                         : OaiCompletionStatus::kFail;
    log.http_status_code = agg->http_status_code;
    log.error_message = agg->error_message;
    log.request_id = agg->request_id;
    log.finish_reason = agg->finish_reason;
    if (!agg->model.empty()) {
      log.model = agg->model;
    }
    log.delta_count = agg->delta_count;

    auto done_state = AllocateShared<OaiCompletionState>(
        info, std::move(assistant), std::move(log), std::move(state));
    on_done(std::move(done_state));
  };

  client->Start([agg, finish, client, parser, delta_cb, reasoning_delta_cb](
                    const HttpSseClientResult& result) {
    agg->http_status_code = result.header.result_int();

    if (result.cancelled) {
      (*finish)(false, "stream cancelled");
      return;
    }

    if (result.ec) {
      (*finish)(false, result.ec.message());
      return;
    }

    if (!IsHttpSuccessStatus(agg->http_status_code)) {
      (*finish)(false,
                "HTTP status " + std::to_string(agg->http_status_code));
      return;
    }

    PullNextStreamChunk(client, parser, agg, finish, delta_cb,
                        reasoning_delta_cb);
  });

  return true;
}

}  // namespace bsrvcore::oai::completion
