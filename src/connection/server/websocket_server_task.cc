/**
 * @file websocket_server_task.cc
 * @brief WebSocket server task scaffolding.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-05
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/server/websocket_server_task.h"

#include <boost/beast/websocket.hpp>
#include <boost/system/errc.hpp>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/internal/connection/server/stream_server_connection.h"
#include "internal/server/http_server_task_detail.h"

namespace bsrvcore {

bool HttpServerTask::IsWebSocketUpgradeMarked() const noexcept {
  const auto& state = GetState();
  std::shared_lock<std::shared_mutex> lock(state.websocket_upgrade_handler_mtx);
  return state.connection_mode.load(std::memory_order_acquire) ==
         HttpTaskConnectionLifecycleMode::kWebSocket;
}

bool HttpServerTask::UpgradeToWebSocket(
    std::unique_ptr<WebSocketHandler> handler) {
  if (!handler || !IsWebSocketRequest() || !IsAvailable()) {
    return false;
  }

  auto& state = GetState();
  auto expected_mode = HttpTaskConnectionLifecycleMode::kAutomatic;
  if (!state.connection_mode.compare_exchange_strong(
          expected_mode, HttpTaskConnectionLifecycleMode::kWebSocket,
          std::memory_order_acq_rel, std::memory_order_acquire)) {
    return false;
  }

  {
    std::unique_lock<std::shared_mutex> lock(
        state.websocket_upgrade_handler_mtx);
    state.websocket_upgrade_handler = std::move(handler);
    if (!state.websocket_upgrade_handler) {
      state.connection_mode.store(HttpTaskConnectionLifecycleMode::kAutomatic,
                                  std::memory_order_release);
      return false;
    }
  }
  state.websocket_upgrade_state.store(
      task_internal::WebSocketUpgradeState::kRequested,
      std::memory_order_release);
  return true;
}

std::shared_ptr<WebSocketServerTask> WebSocketServerTask::Create(
    Executor io_executor, HandlerPtr handler) {
  return AllocateShared<WebSocketServerTask>(
      std::move(io_executor), std::move(handler), nullptr,
      std::shared_ptr<StreamServerConnection>{}, HttpRequest{},
      HttpResponseHeader{});
}

std::shared_ptr<WebSocketServerTask> WebSocketServerTask::CreateFromConnection(
    std::shared_ptr<StreamServerConnection> connection, HttpRequest request,
    HttpResponseHeader response_header, HandlerPtr handler) {
  if (!connection) {
    return nullptr;
  }

  return AllocateShared<WebSocketServerTask>(
      connection->GetIoExecutor(), std::move(handler), connection->GetServer(),
      std::move(connection), std::move(request), std::move(response_header));
}

void WebSocketServerTask::Start() {
  if (started_ || cancelled_) {
    return;
  }

  if (!PrecheckStart()) {
    return;
  }

  StartAccept();
}

bool WebSocketServerTask::PrecheckStart() {
  if (server_ != nullptr && !server_->IsRunning()) {
    cancelled_ = true;
    NotifyClose(make_error_code(boost::system::errc::operation_canceled));
    return false;
  }

  if (!connection_) {
    cancelled_ = true;
    NotifyClose(make_error_code(boost::system::errc::not_connected));
    return false;
  }

  return true;
}

void WebSocketServerTask::StartAccept() {
  auto self = shared_from_this();
  connection_->DoWebSocketAccept(
      std::move(upgrade_request_), std::move(upgrade_response_header_),
      [self](boost::system::error_code ec) { self->OnAcceptCompleted(ec); });
}

void WebSocketServerTask::OnAcceptCompleted(boost::system::error_code ec) {
  if (ec) {
    cancelled_ = true;
    NotifyError(ec, "WebSocket async_accept failed");
    NotifyCloseOnce(ec);
    return;
  }

  started_ = true;
  NotifyOpen();
  BeginReadLoop();
}

void WebSocketServerTask::BeginReadLoop() {
  if (cancelled_) {
    return;
  }

  if (!connection_) {
    cancelled_ = true;
    NotifyCloseOnce(make_error_code(boost::system::errc::not_connected));
    return;
  }

  auto self = shared_from_this();
  connection_->DoWebSocketRead(
      [self](boost::system::error_code ec, WebSocketMessage message) {
        if (ec) {
          if (ec == boost::beast::websocket::error::closed) {
            self->cancelled_ = true;
            self->NotifyCloseOnce(boost::system::error_code{});
            return;
          }

          self->cancelled_ = true;
          self->NotifyError(ec, "WebSocket read failed");
          self->NotifyCloseOnce(ec);
          return;
        }

        self->NotifyReadMessage(std::move(message));
        self->BeginReadLoop();
      });
}

void WebSocketServerTask::Cancel() {
  if (cancelled_) {
    return;
  }

  cancelled_ = true;

  if (connection_) {
    auto self = shared_from_this();
    connection_->DoWebSocketClose(
        [self](boost::system::error_code ec) { self->NotifyCloseOnce(ec); });
    return;
  }

  NotifyCloseOnce(boost::system::error_code{});
}

bool WebSocketServerTask::WriteMessage(std::string payload, bool binary) {
  if (cancelled_ || !started_) {
    return false;
  }

  if (!connection_) {
    cancelled_ = true;
    NotifyCloseOnce(make_error_code(boost::system::errc::not_connected));
    return false;
  }

  auto self = shared_from_this();
  connection_->DoWebSocketWrite(
      std::move(payload), binary, [self](boost::system::error_code ec) {
        if (ec) {
          self->NotifyError(ec, "WebSocket write failed");
        }
      });
  return true;
}

bool WebSocketServerTask::WriteControl(WebSocketControlKind kind,
                                       std::string payload) {
  if (cancelled_ || !started_) {
    return false;
  }

  if (!connection_) {
    cancelled_ = true;
    NotifyCloseOnce(make_error_code(boost::system::errc::not_connected));
    return false;
  }

  auto self = shared_from_this();
  connection_->DoWebSocketControl(
      kind, std::move(payload), [self](boost::system::error_code ec) {
        if (ec) {
          self->NotifyError(ec, "WebSocket control write failed");
        }
      });
  return true;
}

void WebSocketServerTask::NotifyCloseOnce(boost::system::error_code ec) {
  if (close_notified_) {
    return;
  }

  close_notified_ = true;
  NotifyClose(ec);
}

WebSocketServerTask::~WebSocketServerTask() = default;

WebSocketServerTask::WebSocketServerTask(
    Executor io_executor, HandlerPtr handler, HttpServer* server,
    std::shared_ptr<StreamServerConnection> connection, HttpRequest request,
    HttpResponseHeader response_header)
    : WebSocketTaskBase(std::move(handler)),
      io_executor_(std::move(io_executor)),
      server_(server),
      connection_(std::move(connection)),
      upgrade_request_(std::move(request)),
      upgrade_response_header_(std::move(response_header)) {}

}  // namespace bsrvcore
