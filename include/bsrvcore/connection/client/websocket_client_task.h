/**
 * @file websocket_client_task.h
 * @brief WebSocket client task API.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-05
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_WEBSOCKET_CLIENT_TASK_H_
#define BSRVCORE_CONNECTION_CLIENT_WEBSOCKET_CLIENT_TASK_H_

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <memory>
#include <optional>
#include <string>

#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/websocket/websocket_task_base.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore {

class WebSocketClientTask
    : public WebSocketTaskBase,
      public std::enable_shared_from_this<WebSocketClientTask>,
      public NonCopyableNonMovable<WebSocketClientTask> {
 public:
  using Executor = HttpClientTask::Executor;
  using HttpDoneCallback = HttpClientTask::Callback;

  static std::shared_ptr<WebSocketClientTask> CreateHttp(
      Executor io_executor, std::string host, std::string port,
      std::string target, HandlerPtr handler, HttpClientOptions options = {});

  static std::shared_ptr<WebSocketClientTask> CreateHttps(
      Executor io_executor, std::string host, std::string port,
      std::string target, HandlerPtr handler, HttpClientOptions options = {});

  static std::shared_ptr<WebSocketClientTask> CreateHttps(
      Executor io_executor, SslContextPtr ssl_ctx, std::string host,
      std::string port, std::string target, HandlerPtr handler,
      HttpClientOptions options = {});

  static std::shared_ptr<WebSocketClientTask> CreateFromUrl(
      Executor io_executor, std::string url, HandlerPtr handler,
      HttpClientOptions options = {});

  static std::shared_ptr<WebSocketClientTask> CreateFromUrl(
      Executor io_executor, SslContextPtr ssl_ctx, std::string url,
      HandlerPtr handler, HttpClientOptions options = {});

  std::shared_ptr<WebSocketClientTask> OnHttpDone(HttpDoneCallback cb);

  HttpClientRequest& Request() noexcept;
  void AttachSession(std::weak_ptr<HttpClientSession> session);

  void Start() override;
  void Cancel() override;

  bool WriteMessage(std::string payload, bool binary = false) override;
  bool WriteControl(WebSocketControlKind kind,
                    std::string payload = {}) override;

  ~WebSocketClientTask() override;

  explicit WebSocketClientTask(Executor io_executor, std::string host,
                               std::string port, std::string target,
                               HandlerPtr handler, HttpClientOptions options,
                               bool use_ssl = false,
                               SslContextPtr ssl_ctx = {});

 private:
  bool PrepareStart();
  bool CreateTransport();
  void StartResolve();
  void OnResolveCompleted(boost::system::error_code ec,
                          TcpResolverResults results);
  void StartTcpConnect(const TcpResolverResults& results);
  void OnTcpConnectCompleted(boost::system::error_code ec);
  void StartTlsHandshake();
  void OnTlsHandshakeCompleted(boost::system::error_code ec);
  void StartWebSocketHandshake();
  void OnWebSocketHandshakeCompleted(
      boost::system::error_code ec,
      std::shared_ptr<WebSocketResponse> response);
  void SyncHandshakeSetCookies(const WebSocketResponse& response);
  void SetCreateError(boost::system::error_code ec, std::string message);

  bool WritePingControl(std::string payload);
  bool WritePongControl(std::string payload);
  bool WriteCloseControl();
  void BeginReadLoop();
  void NotifyCloseOnce(boost::system::error_code ec);
  void FailAndClose(boost::system::error_code ec, std::string message);
  void EmitHttpDone(boost::system::error_code ec,
                    HttpResponseHeader header = {},
                    HttpClientResponse response = {});

  Executor io_executor_;
  std::string host_;
  std::string port_;
  std::string target_;
  HttpClientOptions options_;
  bool use_ssl_{false};
  SslContextPtr ssl_ctx_;
  HttpClientRequest request_;
  std::unique_ptr<TcpResolver> resolver_;
  std::unique_ptr<WebSocketStream> ws_stream_;
  std::unique_ptr<SecureWebSocketStream> wss_stream_;
  FlatBuffer ws_read_buffer_;
  std::weak_ptr<HttpClientSession> session_;
  HttpDoneCallback on_http_done_;
  bool start_requested_{false};
  bool opened_{false};
  bool cancelled_{false};
  bool close_notified_{false};
  std::optional<boost::system::error_code> create_error_;
  std::string create_error_message_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_WEBSOCKET_CLIENT_TASK_H_
