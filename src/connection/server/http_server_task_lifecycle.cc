/**
 * @file http_server_task_lifecycle.cc
 * @brief Pre/service/post lifecycle implementation for HTTP server tasks.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <boost/asio/bind_allocator.hpp>
#include <cassert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/connection/server/websocket_server_task.h"
#include "internal/server/http_server_task_detail.h"

namespace bsrvcore {

namespace {

using connection_internal::helper::CreateTaskState;
using connection_internal::helper::FinalizeManualConnectionAction;
using connection_internal::helper::GetConnection;
using connection_internal::helper::IsTaskEnvironmentAvailable;
using connection_internal::helper::ResponseWriteStageResult;
using connection_internal::helper::SubmitFinalResponseWriteIfNeeded;
using connection_internal::helper::TryCloseConnection;
using task_internal::WebSocketUpgradeState;

WebSocketTaskBase::HandlerPtr TakePendingWebSocketHandler(
    const std::shared_ptr<task_internal::HttpTaskSharedState>& state) {
  if (!state) {
    return nullptr;
  }

  std::unique_lock<std::shared_mutex> lock(
      state->websocket_upgrade_handler_mtx);
  return std::move(state->websocket_upgrade_handler);
}

bool TryStartWebSocketUpgradeTask(
    const std::shared_ptr<task_internal::HttpTaskSharedState>& state,
    HttpRequest request, HttpResponseHeader response_header) {
  if (!state ||
      state->websocket_upgrade_state.load(std::memory_order_acquire) !=
          WebSocketUpgradeState::kHeaderPrepared) {
    return false;
  }

  auto handler = TakePendingWebSocketHandler(state);
  auto conn = GetConnection(state);
  if (!handler || !conn) {
    TryCloseConnection(state);
    return false;
  }

  auto websocket_task = WebSocketServerTask::CreateFromConnection(
      conn, std::move(request), std::move(response_header), std::move(handler));
  if (!websocket_task) {
    TryCloseConnection(state);
    return false;
  }

  websocket_task->Start();
  state->websocket_upgrade_state.store(WebSocketUpgradeState::kTaskStarted,
                                       std::memory_order_release);
  state->conn.store(nullptr);
  return true;
}

void FinalizeAutomaticMode(
    const std::shared_ptr<task_internal::HttpTaskSharedState>& state) {
  const auto write_stage = SubmitFinalResponseWriteIfNeeded(state);
  if (write_stage == ResponseWriteStageResult::kConnectionUnavailable ||
      write_stage == ResponseWriteStageResult::kWriteSubmitted) {
    // kWriteSubmitted means DoWriteResponse completion handles
    // keep-alive routing (DoCycle vs DoClose).
    return;
  }

  TryCloseConnection(state);
}

void FinalizeManualMode(
    const std::shared_ptr<task_internal::HttpTaskSharedState>& state) {
  const auto write_stage = SubmitFinalResponseWriteIfNeeded(state);
  if (write_stage == ResponseWriteStageResult::kConnectionUnavailable) {
    return;
  }

  if (write_stage != ResponseWriteStageResult::kSkippedManualManagement) {
    TryCloseConnection(state);
    return;
  }

  FinalizeManualConnectionAction(state);
}

void FinalizeWebSocketMode(
    const std::shared_ptr<task_internal::HttpTaskSharedState>& state) {
  const auto write_stage = SubmitFinalResponseWriteIfNeeded(state);
  if (write_stage == ResponseWriteStageResult::kConnectionUnavailable) {
    return;
  }

  if (write_stage != ResponseWriteStageResult::kWebSocketAcceptPending) {
    TryCloseConnection(state);
    return;
  }

  auto request = state->req;
  auto response_header =
      connection_internal::helper::MakeResponseHeaderSnapshot(state->resp);
  (void)TryStartWebSocketUpgradeTask(state, std::move(request),
                                     std::move(response_header));
}

void FinalizePostPhaseConnection(
    const std::shared_ptr<task_internal::HttpTaskSharedState>& state) {
  if (!state) {
    return;
  }

  switch (state->connection_mode.load(std::memory_order_acquire)) {
    case HttpTaskConnectionLifecycleMode::kAutomatic:
      FinalizeAutomaticMode(state);
      return;
    case HttpTaskConnectionLifecycleMode::kManual:
      FinalizeManualMode(state);
      return;
    case HttpTaskConnectionLifecycleMode::kWebSocket:
      FinalizeWebSocketMode(state);
      return;
  }
}

template <typename Task>
inline void DestroyLifecycleTask(
    const std::shared_ptr<task_internal::HttpTaskSharedState>& state,
    Task* ptr) {
  if (!ptr) {
    return;
  }

  ptr->~Task();
  connection_internal::helper::DestroyTaskObject(state, ptr, sizeof(Task),
                                                 alignof(Task));
}

}  // namespace

namespace task_internal {

void HttpPreTaskDeleter::operator()(HttpPreServerTask* ptr) const {
  DestroyLifecycleTask(state, ptr);
  if (!state) {
    return;
  }

  // Releasing the pre task is the exact point where pre hooks stop mutating
  // shared request state, so the next lifecycle hop can safely build the
  // service task.
  if (state->route_result.handler == nullptr ||
      !IsTaskEnvironmentAvailable(state)) {
    TryCloseConnection(state);
    return;
  }

  auto* raw =
      connection_internal::helper::AllocateTaskObject<HttpServerTask>(state);
  new (raw) HttpServerTask(state);
  auto task = std::shared_ptr<HttpServerTask>(raw, HttpServerTaskDeleter{state},
                                              state->handler_alloc);
  task->Start();
}

void HttpServerTaskDeleter::operator()(HttpServerTask* ptr) const {
  DestroyLifecycleTask(state, ptr);
  if (!state) {
    return;
  }

  // Service handlers may hold shared_ptrs to the task, so the custom deleter
  // is the first reliable place to decide whether to finalize immediately or
  // hand off to post aspects.
  if (!IsTaskEnvironmentAvailable(state)) {
    TryCloseConnection(state);
    return;
  }

  auto* raw =
      connection_internal::helper::AllocateTaskObject<HttpPostServerTask>(
          state);
  new (raw) HttpPostServerTask(state);
  auto task = std::shared_ptr<HttpPostServerTask>(
      raw, HttpPostTaskDeleter{state}, state->handler_alloc);
  task->Start();
}

void HttpPostTaskDeleter::operator()(HttpPostServerTask* ptr) const {
  DestroyLifecycleTask(state, ptr);
  if (!state) {
    return;
  }

  if (!IsTaskEnvironmentAvailable(state)) {
    TryCloseConnection(state);
    return;
  }

  FinalizePostPhaseConnection(state);
}

}  // namespace task_internal

std::shared_ptr<HttpPreServerTask> HttpPreServerTask::Create(
    HttpRequest req, HttpRouteResult route_result,
    std::shared_ptr<StreamServerConnection> conn) {
  // All three lifecycle task objects share one HttpTaskSharedState instance so
  // request/response data, cookies, and connection state survive phase hops.
  auto state =
      CreateTaskState(std::move(req), std::move(route_result), std::move(conn));
  auto* raw =
      connection_internal::helper::AllocateTaskObject<HttpPreServerTask>(state);
  new (raw) HttpPreServerTask(state);
  return std::shared_ptr<HttpPreServerTask>(
      raw, task_internal::HttpPreTaskDeleter{state}, state->handler_alloc);
}

HttpPreServerTask::HttpPreServerTask(
    std::shared_ptr<task_internal::HttpTaskSharedState> state)
    : HttpTaskBase(std::move(state)) {}

HttpPreServerTask::~HttpPreServerTask() = default;

void HttpPreServerTask::Start() {
  auto conn = GetState().conn.load();
  if (!conn) {
    return;
  }

  auto self = shared_from_this();
  // Lifecycle work always enters via the connection executor so request parse,
  // handler execution, and response write sequencing stay serialized per
  // connection without an extra strand object at the task layer.
  conn->DispatchToConnectionExecutor(boost::asio::bind_allocator(
      GetState().handler_alloc, [self = std::move(self)]() mutable {
        RunScheduledPrePhase(std::move(self));
      }));
}

void HttpPreServerTask::RunScheduledPrePhase(
    const std::shared_ptr<HttpPreServerTask>& self) {
  self->DoPreService(0);
}

void HttpPreServerTask::DoPreService(std::size_t curr_idx) {
  auto& state = GetState();
  if (curr_idx >= state.route_result.aspects.size()) {
    return;
  }

  auto self = shared_from_this();
  while (curr_idx < state.route_result.aspects.size()) {
    try {
      // Pre aspects run in registration order and share mutable access to the
      // eventual response. Exceptions are swallowed so one hook cannot abort
      // the entire request pipeline implicitly.
      state.route_result.aspects[curr_idx]->PreService(self);
    } catch (...) {
    }
    ++curr_idx;
  }
}

HttpServerTask::HttpServerTask(HttpRequest req, HttpRouteResult route_result,
                               std::shared_ptr<StreamServerConnection> conn)
    : HttpTaskBase(CreateTaskState(std::move(req), std::move(route_result),
                                   std::move(conn))) {}

HttpServerTask::HttpServerTask(
    std::shared_ptr<task_internal::HttpTaskSharedState> state)
    : HttpTaskBase(std::move(state)) {}

HttpServerTask::~HttpServerTask() = default;

void HttpServerTask::Start() {
  auto conn = GetState().conn.load();
  if (!conn) {
    return;
  }

  auto self = shared_from_this();
  conn->DispatchToConnectionExecutor(boost::asio::bind_allocator(
      GetState().handler_alloc, [self = std::move(self)]() mutable {
        RunScheduledServicePhase(std::move(self));
      }));
}

void HttpServerTask::RunScheduledServicePhase(
    const std::shared_ptr<HttpServerTask>& self) {
  self->DoService();
}

void HttpServerTask::DoService() {
  auto& state = GetState();
  if (state.route_result.handler == nullptr) {
    return;
  }

  auto self = shared_from_this();
  try {
    // Holding shared_ptr<HttpServerTask> beyond this call intentionally delays
    // the custom-deleter transition into the post phase. That is how async
    // handlers defer response finalization without extra state machines.
    state.route_result.handler->Service(self);
  } catch (...) {
  }
}

std::shared_ptr<HttpServerTask> HttpServerTask::Create(
    HttpRequest req, HttpRouteResult route_result,
    std::shared_ptr<StreamServerConnection> conn) {
  auto state =
      CreateTaskState(std::move(req), std::move(route_result), std::move(conn));
  auto* raw =
      connection_internal::helper::AllocateTaskObject<HttpServerTask>(state);
  new (raw) HttpServerTask(state);
  return std::shared_ptr<HttpServerTask>(
      raw, task_internal::HttpServerTaskDeleter{state}, state->handler_alloc);
}

HttpPostServerTask::HttpPostServerTask(
    std::shared_ptr<task_internal::HttpTaskSharedState> state)
    : HttpTaskBase(std::move(state)) {}

HttpPostServerTask::~HttpPostServerTask() = default;

void HttpPostServerTask::Start() {
  auto& state = GetState();
  if (state.route_result.aspects.empty()) {
    return;
  }

  const auto last_idx = state.route_result.aspects.size() - 1;
  auto conn = state.conn.load();
  if (!conn) {
    return;
  }

  auto self = shared_from_this();
  conn->DispatchToConnectionExecutor(boost::asio::bind_allocator(
      state.handler_alloc, [self = std::move(self), last_idx]() mutable {
        // Post hooks unwind in reverse order, mirroring middleware stacks where
        // the innermost route-local aspect exits before outer/global aspects.
        RunScheduledPostPhase(std::move(self), last_idx);
      }));
}

void HttpPostServerTask::RunScheduledPostPhase(
    const std::shared_ptr<HttpPostServerTask>& self, std::size_t curr_idx) {
  self->DoPostService(curr_idx);
}

void HttpPostServerTask::DoPostService(std::size_t curr_idx) {
  auto& state = GetState();
  if (curr_idx >= state.route_result.aspects.size()) {
    assert(false);
    return;
  }

  auto self = shared_from_this();
  while (true) {
    try {
      state.route_result.aspects[curr_idx]->PostService(self);
    } catch (...) {
    }
    if (curr_idx == 0) {
      return;
    }
    --curr_idx;
  }
}

}  // namespace bsrvcore
