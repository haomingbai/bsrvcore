/**
 * @file websocket_server_task.h
 * @brief WebSocket server task API.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-05
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_SERVER_WEBSOCKET_SERVER_TASK_H_
#define BSRVCORE_CONNECTION_SERVER_WEBSOCKET_SERVER_TASK_H_

#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/system/errc.hpp>
#include <memory>
#include <string>

#include "bsrvcore/connection/websocket/websocket_task_base.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"

namespace bsrvcore {

class HttpServer;
class StreamServerConnection;

/**
 * @brief Server-side WebSocket task created from an HTTP upgrade connection.
 *
 * The task accepts the WebSocket upgrade on an existing server connection and
 * then forwards lifecycle, message, and control events to WebSocketHandler.
 */
class WebSocketServerTask
    : public WebSocketTaskBase,
      public std::enable_shared_from_this<WebSocketServerTask>,
      public NonCopyableNonMovable<WebSocketServerTask> {
 public:
  /** @brief Executor type used by server WebSocket operations. */
  using Executor = IoExecutor;

  /**
   * @brief Create a detached server WebSocket task shell.
   *
   * @param io_executor Executor used for WebSocket I/O.
   * @param handler WebSocket event handler.
   * @return Newly created server WebSocket task.
   */
  static std::shared_ptr<WebSocketServerTask> Create(Executor io_executor,
                                                     HandlerPtr handler);

  /**
   * @brief Create a task bound to an accepted HTTP upgrade connection.
   *
   * @param connection Accepted server connection that owns the stream.
   * @param request HTTP upgrade request.
   * @param response_header Response headers to include in the accept response.
   * @param handler WebSocket event handler.
   * @return Newly created server WebSocket task.
   */
  static std::shared_ptr<WebSocketServerTask> CreateFromConnection(
      std::shared_ptr<StreamServerConnection> connection, HttpRequest request,
      HttpResponseHeader response_header, HandlerPtr handler);

  /** @brief Start the WebSocket accept and read loop. */
  void Start() override;
  /** @brief Cancel the task and close the WebSocket stream. */
  void Cancel() override;

  /**
   * @brief Queue a WebSocket text or binary message write.
   *
   * @param payload Message payload to send.
   * @param binary Whether to send as binary instead of text.
   * @return True when the message was queued.
   */
  bool WriteMessage(std::string payload, bool binary = false) override;
  /**
   * @brief Queue a WebSocket ping, pong, or close control frame.
   *
   * @param kind Control frame kind.
   * @param payload Optional control frame payload.
   * @return True when the control frame was queued.
   */
  bool WriteControl(WebSocketControlKind kind,
                    std::string payload = {}) override;

  /** @brief Destroy the task. */
  ~WebSocketServerTask() override;

  /**
   * @brief Construct a server WebSocket task; prefer CreateFromConnection().
   *
   * @param io_executor Executor used for WebSocket I/O.
   * @param handler WebSocket event handler.
   * @param server Owning server pointer, or null for detached shells.
   * @param connection Accepted server connection that owns the stream.
   * @param request HTTP upgrade request.
   * @param response_header Response headers for the accept response.
   */
  explicit WebSocketServerTask(
      Executor io_executor, HandlerPtr handler, HttpServer* server,
      std::shared_ptr<StreamServerConnection> connection, HttpRequest request,
      HttpResponseHeader response_header);

 private:
  bool PrecheckStart();
  void StartAccept();
  void OnAcceptCompleted(boost::system::error_code ec);

  void BeginReadLoop();
  void NotifyCloseOnce(boost::system::error_code ec);

  Executor io_executor_;
  HttpServer* server_{nullptr};
  std::shared_ptr<StreamServerConnection> connection_;
  HttpRequest upgrade_request_;
  HttpResponseHeader upgrade_response_header_;
  bool started_{false};
  bool cancelled_{false};
  bool close_notified_{false};
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_SERVER_WEBSOCKET_SERVER_TASK_H_
