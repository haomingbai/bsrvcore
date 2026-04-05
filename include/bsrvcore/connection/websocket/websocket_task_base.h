/**
 * @file websocket_task_base.h
 * @brief Shared task/handler abstractions for WebSocket server and client APIs.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-05
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_WEBSOCKET_WEBSOCKET_TASK_BASE_H_
#define BSRVCORE_CONNECTION_WEBSOCKET_WEBSOCKET_TASK_BASE_H_

#include <boost/system/error_code.hpp>
#include <memory>
#include <optional>
#include <string>

#include "bsrvcore/core/trait.h"

namespace bsrvcore {

enum class WebSocketControlKind {
  kPing,
  kPong,
  kClose,
};

struct WebSocketMessage : public CopyableMovable<WebSocketMessage> {
  std::string payload;
  bool binary{false};
};

class WebSocketHandler : public NonCopyableNonMovable<WebSocketHandler> {
 public:
  virtual void OnOpen() {}
  virtual void OnReadMessage(const WebSocketMessage& message) = 0;
  virtual void OnError(boost::system::error_code ec,
                       const std::string& message) = 0;
  virtual void OnClose(boost::system::error_code ec) = 0;
  virtual ~WebSocketHandler() = default;
};

class WebSocketTaskBase : public NonCopyableNonMovable<WebSocketTaskBase> {
 public:
  using HandlerPtr = std::unique_ptr<WebSocketHandler>;

  virtual void Start() = 0;
  virtual void Cancel() = 0;

  virtual bool WriteMessage(std::string payload, bool binary = false) = 0;
  virtual bool WriteControl(WebSocketControlKind kind,
                            std::string payload = {}) = 0;

  [[nodiscard]] const WebSocketMessage* CurrentMessage() const noexcept;
  [[nodiscard]] bool HasCurrentMessage() const noexcept;

  virtual ~WebSocketTaskBase() = default;

 protected:
  explicit WebSocketTaskBase(HandlerPtr handler);

  void NotifyOpen();
  void NotifyReadMessage(WebSocketMessage message);
  void NotifyError(boost::system::error_code ec, std::string message);
  void NotifyClose(boost::system::error_code ec);

 private:
  HandlerPtr handler_;
  std::optional<WebSocketMessage> current_message_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_WEBSOCKET_WEBSOCKET_TASK_BASE_H_
