/**
 * @file oai_completion_response.cc
 * @brief Response parsing and stream helpers for OAI completion facade.
 */

#include <atomic>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bsrvcore/connection/client/http_sse_client_task.h"
#include "bsrvcore/connection/client/sse_event_parser.h"
#include "bsrvcore/oai/completion/oai_completion.h"
#include "oai_completion_detail.h"

namespace bsrvcore::oai::completion::detail {

namespace json = boost::json;

namespace {

std::optional<std::string> JsonStringFromObjectField(const json::object& obj,
                                                     const char* key) {
  const json::value* value = obj.if_contains(key);
  if (value == nullptr || !value->is_string()) {
    return std::nullopt;
  }
  return std::string(value->as_string().c_str());
}

std::optional<json::value> TryParseJsonValue(const std::string& text) {
  boost::system::error_code parse_ec;
  json::value value = json::parse(text, parse_ec);
  if (parse_ec) {
    return std::nullopt;
  }
  return value;
}

json::value ParseToolCallArgumentsValue(const json::value& arguments) {
  if (!arguments.is_string()) {
    return arguments;
  }

  const std::string text(arguments.as_string().c_str());
  if (auto parsed = TryParseJsonValue(text)) {
    return std::move(*parsed);
  }
  return json::value(text);
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
        call.arguments = ParseToolCallArgumentsValue(*args_value);
      }
    }

    if (!call.id.empty() || !call.name.empty() || !call.arguments.is_null()) {
      out.push_back(std::move(call));
    }
  }
  return out;
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

  if (auto finish_reason =
          JsonStringFromObjectField(choice_obj, "finish_reason")) {
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
                                      const char* key, std::string* out_text) {
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
  out.role = JsonStringFromObjectField(message_obj, "role")
                 .value_or(std::string("assistant"));
  ParseMessageTextFieldOrSerialize(message_obj, "content", &out.message);
  ParseMessageTextFieldOrSerialize(message_obj, "reasoning_content",
                                   &out.reasoning);

  const json::value* tool_calls_value = message_obj.if_contains("tool_calls");
  if (tool_calls_value != nullptr && tool_calls_value->is_array()) {
    out.tool_calls = ParseToolCallsFromArray(tool_calls_value->as_array());
  }

  *message_out = std::move(out);
}

const json::object* TryGetStreamFirstChoiceObject(
    const json::object& root_obj) {
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
          acc.arguments_text.append(args_value->as_string().c_str());
        } else {
          acc.arguments_text.append(json::serialize(*args_value));
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
  boost::system::error_code ec;
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
    if (auto parsed_finish =
            JsonStringFromObjectField(*choice_obj, "finish_reason")) {
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

}  // namespace

std::string ExtractErrorMessageFromJsonBody(const std::string& body) {
  boost::system::error_code ec;
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

bool ParseCompletionResponseBody(const std::string& body,
                                 OaiMessage* message_out, OaiRequestLog* log,
                                 std::string* error_out) {
  boost::system::error_code ec;
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

std::vector<OaiToolCall> BuildToolCallsFromAccumulation(
    const std::map<std::size_t, ToolCallAccumulator>& tool_calls) {
  std::vector<OaiToolCall> out;
  out.reserve(tool_calls.size());
  for (const auto& [_, acc] : tool_calls) {
    OaiToolCall call;
    call.id = acc.id;
    call.name = acc.name;

    if (!acc.arguments_text.empty()) {
      if (auto parsed = TryParseJsonValue(acc.arguments_text)) {
        call.arguments = std::move(*parsed);
      } else {
        call.arguments = acc.arguments_text;
      }
    }

    if (!call.id.empty() || !call.name.empty() || !call.arguments.is_null()) {
      out.push_back(std::move(call));
    }
  }
  return out;
}

void PullNextStreamChunk(
    const std::shared_ptr<HttpSseClientTask>& client,
    const std::shared_ptr<SseEventParser>& parser,
    const std::shared_ptr<StreamAggregate>& agg,
    const std::shared_ptr<std::function<void(bool, std::string)>>& finish,
    const std::shared_ptr<OaiCompletionFactory::StreamDeltaCallback>& on_delta,
    const std::shared_ptr<OaiCompletionFactory::StreamDeltaCallback>&
        on_reasoning_delta) {
  client->Next([client, parser, agg, finish, on_delta,
                on_reasoning_delta](const HttpSseClientResult& result) mutable {
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
        const StreamEventAction action = ProcessStreamEventData(
            event.data, agg.get(), on_delta, on_reasoning_delta, &parse_error);
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

}  // namespace bsrvcore::oai::completion::detail
