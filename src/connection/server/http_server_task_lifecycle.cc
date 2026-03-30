/**
 * @file http_server_task_lifecycle.cc
 * @brief Pre/service/post lifecycle implementation for HTTP server tasks.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/server/http_server_task.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>

#include "bsrvcore/internal/connection/server/http_server_task_detail.h"

namespace bsrvcore {

namespace {

using connection_internal::helper::CreateTaskState;
using connection_internal::helper::FinalizeResponse;
using connection_internal::helper::IsTaskEnvironmentAvailable;
using connection_internal::helper::TryCloseConnection;

template <typename Task>
inline void DestroyLifecycleTask(const std::shared_ptr<task_internal::HttpTaskSharedState>& state,
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
  if (state->route_result.handler == nullptr || !IsTaskEnvironmentAvailable(state)) {
    TryCloseConnection(state);
    return;
  }

  auto* raw =
      connection_internal::helper::AllocateTaskObject<HttpServerTask>(state);
  new (raw) HttpServerTask(state);
  auto task = std::shared_ptr<HttpServerTask>(
      raw, HttpServerTaskDeleter{state}, state->handler_alloc);
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

  if (state->route_result.aspects.empty()) {
    FinalizeResponse(state);
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

  FinalizeResponse(state);
}

}  // namespace task_internal

std::shared_ptr<HttpPreServerTask> HttpPreServerTask::Create(
    HttpRequest req, HttpRouteResult route_result,
    std::shared_ptr<HttpServerConnection> conn) {
  auto state = CreateTaskState(std::move(req), std::move(route_result),
                               std::move(conn));
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
  conn->DispatchToConnectionExecutor(boost::asio::bind_allocator(
      GetState().handler_alloc,
      [self = std::move(self)]() mutable {
        RunScheduledPrePhase(std::move(self));
      }));
}

void HttpPreServerTask::RunScheduledPrePhase(
    std::shared_ptr<HttpPreServerTask> self) {
  self->DoPreService(0);
}

void HttpPreServerTask::DoPreService(std::size_t curr_idx) {
  auto& state = GetState();
  if (curr_idx >= state.route_result.aspects.size()) {
    return;
  }

  auto self = shared_from_this();
  while (curr_idx < state.route_result.aspects.size()) {
    state.route_result.aspects[curr_idx]->PreService(self);
    ++curr_idx;
  }
}

HttpServerTask::HttpServerTask(HttpRequest req, HttpRouteResult route_result,
                               std::shared_ptr<HttpServerConnection> conn)
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
      GetState().handler_alloc,
      [self = std::move(self)]() mutable {
        RunScheduledServicePhase(std::move(self));
      }));
}

void HttpServerTask::RunScheduledServicePhase(
    std::shared_ptr<HttpServerTask> self) {
  self->DoService();
}

void HttpServerTask::DoService() {
  auto& state = GetState();
  if (state.route_result.handler == nullptr) {
    return;
  }

  auto self = shared_from_this();
  state.route_result.handler->Service(self);
}

std::shared_ptr<HttpServerTask> HttpServerTask::Create(
    HttpRequest req, HttpRouteResult route_result,
    std::shared_ptr<HttpServerConnection> conn) {
  auto state = CreateTaskState(std::move(req), std::move(route_result),
                               std::move(conn));
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
      state.handler_alloc,
      [self = std::move(self), last_idx]() mutable {
        RunScheduledPostPhase(std::move(self), last_idx);
      }));
}

void HttpPostServerTask::RunScheduledPostPhase(
    std::shared_ptr<HttpPostServerTask> self, std::size_t curr_idx) {
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
    state.route_result.aspects[curr_idx]->PostService(self);
    if (curr_idx == 0) {
      return;
    }
    --curr_idx;
  }
}

}  // namespace bsrvcore
