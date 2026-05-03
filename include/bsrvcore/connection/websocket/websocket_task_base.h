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

/** @brief WebSocket control frame kind for explicit control writes. */
enum class WebSocketControlKind {
  /** @brief Send a ping frame. */
  kPing,
  /** @brief Send a pong frame. */
  kPong,
  /** @brief Send a close frame. */
  kClose,
};

/** @brief One complete WebSocket message delivered to a handler. */
struct WebSocketMessage : public CopyableMovable<WebSocketMessage> {
  /** @brief Message payload bytes. */
  std::string payload;
  /** @brief Whether the message is binary instead of text. */
  bool binary{false};
};

/** @brief User callback interface for WebSocket lifecycle and messages. */
class WebSocketHandler : public NonCopyableNonMovable<WebSocketHandler> {
 public:
  /** @brief Called after the WebSocket handshake succeeds. */
  virtual void OnOpen() {}
  /**
   * @brief Called when a complete message is received.
   *
   * @param message Received WebSocket message.
   */
  virtual void OnReadMessage(const WebSocketMessage& message) = 0;
  /**
   * @brief Called when the task observes a transport or protocol error.
   *
   * @param ec Error code describing the failure.
   * @param message Human-readable error context.
   */
  virtual void OnError(boost::system::error_code ec,
                       const std::string& message) = 0;
  /**
   * @brief Called once the WebSocket task closes.
   *
   * @param ec Close reason, or success for normal close.
   */
  virtual void OnClose(boost::system::error_code ec) = 0;
  /** @brief Destroy the handler. */
  virtual ~WebSocketHandler() = default;
};

/** @brief Shared base API for client and server WebSocket tasks. */
class WebSocketTaskBase : public NonCopyableNonMovable<WebSocketTaskBase> {
 public:
  /** @brief Owning pointer type for WebSocket handlers. */
  using HandlerPtr = std::unique_ptr<WebSocketHandler>;

  /** @brief Start the task. */
  virtual void Start() = 0;
  /** @brief Cancel the task and close any owned transport. */
  virtual void Cancel() = 0;

  /**
   * @brief Write a text or binary WebSocket message.
   *
   * @param payload Message payload to send.
   * @param binary Whether to send as binary instead of text.
   * @return True when the write was queued.
   */
  virtual bool WriteMessage(std::string payload, bool binary = false) = 0;
  /**
   * @brief Write a WebSocket ping, pong, or close control frame.
   *
   * @param kind Control frame kind.
   * @param payload Optional control frame payload.
   * @return True when the write was queued.
   */
  virtual bool WriteControl(WebSocketControlKind kind,
                            std::string payload = {}) = 0;

  /**
   * @brief Return the message currently being processed by a callback.
   *
   * @return Pointer to the current message, or null outside read callbacks.
   */
  [[nodiscard]] const WebSocketMessage* CurrentMessage() const noexcept;
  /**
   * @brief Whether a message is currently available through CurrentMessage().
   *
   * @return True when CurrentMessage() returns a non-null pointer.
   */
  [[nodiscard]] bool HasCurrentMessage() const noexcept;

  /** @brief Destroy the task base. */
  virtual ~WebSocketTaskBase() = default;

 protected:
  /**
   * @brief Construct with an owned handler.
   *
   * @param handler Handler that receives lifecycle and message callbacks.
   */
  explicit WebSocketTaskBase(HandlerPtr handler);

  /** @brief Notify the handler that the WebSocket is open. */
  void NotifyOpen();
  /**
   * @brief Notify the handler of one complete message.
   *
   * @param message Message to expose during the callback.
   */
  void NotifyReadMessage(WebSocketMessage message);
  /**
   * @brief Notify the handler of an error.
   *
   * @param ec Error code describing the failure.
   * @param message Human-readable error context.
   */
  void NotifyError(boost::system::error_code ec, std::string message);
  /**
   * @brief Notify the handler that the task closed.
   *
   * @param ec Close reason, or success for normal close.
   */
  void NotifyClose(boost::system::error_code ec);

 private:
  HandlerPtr handler_;
  std::optional<WebSocketMessage> current_message_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_WEBSOCKET_WEBSOCKET_TASK_BASE_H_
