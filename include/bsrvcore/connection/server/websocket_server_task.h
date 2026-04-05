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

#include <boost/asio/any_io_executor.hpp>
#include <memory>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/connection/websocket/websocket_task_base.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore {

class StreamServerConnection;
class HttpServer;

class WebSocketServerTask
    : public WebSocketTaskBase,
      public std::enable_shared_from_this<WebSocketServerTask>,
      public NonCopyableNonMovable<WebSocketServerTask> {
 public:
  using Executor = boost::asio::any_io_executor;

  static std::shared_ptr<WebSocketServerTask> Create(Executor io_executor,
                                                     HandlerPtr handler);

  static std::shared_ptr<WebSocketServerTask> CreateFromConnection(
      std::shared_ptr<StreamServerConnection> connection, HttpRequest request,
      HttpResponseHeader response_header, HandlerPtr handler);

  void Start() override;
  void Cancel() override;

  bool WriteMessage(std::string payload, bool binary = false) override;
  bool WriteControl(WebSocketControlKind kind,
                    std::string payload = {}) override;

  ~WebSocketServerTask() override;

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

 private:
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
