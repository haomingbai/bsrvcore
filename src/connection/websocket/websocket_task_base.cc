/**
 * @file websocket_task_base.cc
 * @brief Shared WebSocket task/handler glue implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-05
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/connection/websocket/websocket_task_base.h"

#include <utility>

namespace bsrvcore {

WebSocketTaskBase::WebSocketTaskBase(HandlerPtr handler)
    : handler_(std::move(handler)) {}

const WebSocketMessage* WebSocketTaskBase::CurrentMessage() const noexcept {
  return current_message_ ? &*current_message_ : nullptr;
}

bool WebSocketTaskBase::HasCurrentMessage() const noexcept {
  return current_message_.has_value();
}

void WebSocketTaskBase::NotifyOpen() {
  if (handler_) {
    handler_->OnOpen();
  }
}

void WebSocketTaskBase::NotifyReadMessage(WebSocketMessage message) {
  current_message_ = message;
  if (handler_) {
    handler_->OnReadMessage(*current_message_);
  }
}

void WebSocketTaskBase::NotifyError(boost::system::error_code ec,
                                    std::string message) {
  if (handler_) {
    handler_->OnError(ec, message);
  }
}

void WebSocketTaskBase::NotifyClose(boost::system::error_code ec) {
  if (handler_) {
    handler_->OnClose(ec);
  }
}

}  // namespace bsrvcore
