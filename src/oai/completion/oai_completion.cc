/**
 * @file oai_completion.cc
 * @brief Factory request execution for chat-style OAI completion facade.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-01
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/oai/completion/oai_completion.h"

#include <boost/beast/http.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "oai_completion_detail.h"

namespace bsrvcore::oai::completion {

namespace http = boost::beast::http;

bool OaiCompletionFactory::FetchCompletion(
    StatePtr state, std::shared_ptr<OaiModelInfo> model_info,
    CompletionCallback cb) const {
  return FetchCompletion(std::move(state), {}, std::move(model_info),
                         std::move(cb));
}

bool OaiCompletionFactory::FetchCompletion(
    StatePtr state, const std::vector<OaiToolDefinition>& tools,
    std::shared_ptr<OaiModelInfo> model_info, CompletionCallback cb) const {
  if (!state || !cb || !model_info || !info_ || info_->base_url.empty() ||
      model_info->model.empty()) {
    return false;
  }

  std::string request_body;
  std::string build_error;
  if (!detail::BuildRequestPayload(detail::CollectMessageChain(state), tools,
                                   *model_info, false, &request_body,
                                   &build_error)) {
    OaiMessage assistant;
    assistant.role = "assistant";

    OaiRequestLog log = detail::BuildLogSkeleton(*model_info, false);
    log.error_message = std::move(build_error);
    auto failure_state = AllocateShared<OaiCompletionState>(
        info_, model_info, std::move(assistant), std::move(log),
        std::move(state));
    cb(std::move(failure_state));
    return true;
  }

  HttpClientOptions options;
  options.connect_timeout = std::chrono::seconds(10);
  options.read_header_timeout = std::chrono::seconds(10);
  options.read_body_timeout = std::chrono::seconds(60);

  auto task = HttpClientTask::CreateFromUrl(
      executor_, *ssl_ctx_, detail::BuildCompletionsUrl(info_->base_url),
      http::verb::post, options);

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

  task->OnDone([state = std::move(state), cb = std::move(cb), info = info_,
                model_info](const HttpClientResult& result) mutable {
    OaiMessage assistant;
    assistant.role = "assistant";

    OaiRequestLog log = detail::BuildLogSkeleton(*model_info, false);
    log.http_status_code = result.response.result_int();

    const auto request_id_header = result.response.base().find("x-request-id");
    if (request_id_header != result.response.base().end()) {
      log.request_id = std::string(request_id_header->value().data(),
                                   request_id_header->value().size());
    }

    if (result.ec) {
      log.error_message = result.ec.message();
      auto failure_state = AllocateShared<OaiCompletionState>(
          info, model_info, std::move(assistant), std::move(log),
          std::move(state));
      cb(std::move(failure_state));
      return;
    }

    if (result.cancelled) {
      log.error_message = "request cancelled";
      auto failure_state = AllocateShared<OaiCompletionState>(
          info, model_info, std::move(assistant), std::move(log),
          std::move(state));
      cb(std::move(failure_state));
      return;
    }

    std::string parse_error;
    if (!detail::IsHttpSuccessStatus(log.http_status_code)) {
      parse_error =
          detail::ExtractErrorMessageFromJsonBody(result.response.body());
      log.error_message =
          parse_error.empty()
              ? ("HTTP status " + std::to_string(log.http_status_code))
              : std::move(parse_error);
      auto failure_state = AllocateShared<OaiCompletionState>(
          info, model_info, std::move(assistant), std::move(log),
          std::move(state));
      cb(std::move(failure_state));
      return;
    }

    if (!detail::ParseCompletionResponseBody(result.response.body(), &assistant,
                                             &log, &parse_error)) {
      log.error_message = std::move(parse_error);
      auto failure_state = AllocateShared<OaiCompletionState>(
          info, model_info, std::move(assistant), std::move(log),
          std::move(state));
      cb(std::move(failure_state));
      return;
    }

    log.status = OaiCompletionStatus::kSuccess;
    auto next_state = AllocateShared<OaiCompletionState>(
        info, model_info, std::move(assistant), std::move(log),
        std::move(state));
    cb(std::move(next_state));
  });

  task->Start();
  return true;
}

bool OaiCompletionFactory::FetchStreamCompletion(
    StatePtr state, std::shared_ptr<OaiModelInfo> model_info,
    StreamDoneCallback on_done, StreamDeltaCallback on_delta) const {
  return FetchStreamCompletion(
      std::move(state), std::vector<OaiToolDefinition>{}, std::move(model_info),
      std::move(on_done), std::move(on_delta), StreamDeltaCallback{});
}

bool OaiCompletionFactory::FetchStreamCompletion(
    StatePtr state, std::shared_ptr<OaiModelInfo> model_info,
    StreamDoneCallback on_done, StreamDeltaCallback on_delta,
    StreamDeltaCallback on_reasoning_delta) const {
  return FetchStreamCompletion(
      std::move(state), std::vector<OaiToolDefinition>{}, std::move(model_info),
      std::move(on_done), std::move(on_delta), std::move(on_reasoning_delta));
}

bool OaiCompletionFactory::FetchStreamCompletion(
    StatePtr state, const std::vector<OaiToolDefinition>& tools,
    std::shared_ptr<OaiModelInfo> model_info, StreamDoneCallback on_done,
    StreamDeltaCallback on_delta) const {
  return FetchStreamCompletion(std::move(state), tools, std::move(model_info),
                               std::move(on_done), std::move(on_delta),
                               StreamDeltaCallback{});
}

bool OaiCompletionFactory::FetchStreamCompletion(
    StatePtr state, const std::vector<OaiToolDefinition>& tools,
    std::shared_ptr<OaiModelInfo> model_info, StreamDoneCallback on_done,
    StreamDeltaCallback on_delta,
    StreamDeltaCallback on_reasoning_delta) const {
  if (!state || !on_done || (!on_delta && !on_reasoning_delta) || !model_info ||
      !info_ || info_->base_url.empty() || model_info->model.empty()) {
    return false;
  }

  std::string request_body;
  std::string build_error;
  if (!detail::BuildRequestPayload(detail::CollectMessageChain(state), tools,
                                   *model_info, true, &request_body,
                                   &build_error)) {
    OaiMessage assistant;
    assistant.role = "assistant";

    OaiRequestLog log = detail::BuildLogSkeleton(*model_info, true);
    log.error_message = std::move(build_error);
    auto failure_state = AllocateShared<OaiCompletionState>(
        info_, model_info, std::move(assistant), std::move(log),
        std::move(state));
    on_done(std::move(failure_state));
    return true;
  }

  HttpSseClientOptions options;
  options.connect_timeout = std::chrono::seconds(10);
  options.read_header_timeout = std::chrono::seconds(10);
  options.read_body_timeout = std::chrono::seconds(60);

  auto client = HttpSseClientTask::CreateFromUrl(
      executor_, *ssl_ctx_, detail::BuildCompletionsUrl(info_->base_url),
      options);

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
  auto agg = AllocateShared<detail::StreamAggregate>();

  std::shared_ptr<StreamDeltaCallback> delta_cb;
  if (on_delta) {
    delta_cb = AllocateShared<StreamDeltaCallback>(std::move(on_delta));
  }

  std::shared_ptr<StreamDeltaCallback> reasoning_delta_cb;
  if (on_reasoning_delta) {
    reasoning_delta_cb =
        AllocateShared<StreamDeltaCallback>(std::move(on_reasoning_delta));
  }

  auto finish = AllocateShared<std::function<void(bool, std::string)>>();
  *finish = [agg, on_done = std::move(on_done), info = info_, model_info,
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
    assistant.tool_calls =
        detail::BuildToolCallsFromAccumulation(agg->tool_calls);
    assistant.reasoning = agg->accumulated_reasoning;

    OaiRequestLog log = detail::BuildLogSkeleton(*model_info, true);
    log.status =
        success ? OaiCompletionStatus::kSuccess : OaiCompletionStatus::kFail;
    log.http_status_code = agg->http_status_code;
    log.error_message = agg->error_message;
    log.request_id = agg->request_id;
    log.finish_reason = agg->finish_reason;
    if (!agg->model.empty()) {
      log.model = agg->model;
    }
    log.delta_count = agg->delta_count;

    auto done_state = AllocateShared<OaiCompletionState>(
        info, model_info, std::move(assistant), std::move(log),
        std::move(state));
    on_done(std::move(done_state));
  };

  client->Start([agg, finish, client, parser, delta_cb,
                 reasoning_delta_cb](const HttpSseClientResult& result) {
    agg->http_status_code = result.header.result_int();

    if (result.cancelled) {
      (*finish)(false, "stream cancelled");
      return;
    }

    if (result.ec) {
      (*finish)(false, result.ec.message());
      return;
    }

    if (!detail::IsHttpSuccessStatus(agg->http_status_code)) {
      (*finish)(false, "HTTP status " + std::to_string(agg->http_status_code));
      return;
    }

    detail::PullNextStreamChunk(client, parser, agg, finish, delta_cb,
                                reasoning_delta_cb);
  });

  return true;
}

}  // namespace bsrvcore::oai::completion
