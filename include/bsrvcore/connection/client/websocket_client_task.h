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

#include "bsrvcore/connection/client/client_websocket_stream.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/stream_slot.h"
#include "bsrvcore/connection/websocket/websocket_task_base.h"
#include "bsrvcore/core/trait.h"

// Forward declarations for assembled mode.
namespace bsrvcore {
class RequestAssembler;
class StreamBuilder;
}  // namespace bsrvcore

namespace bsrvcore {

class WebSocketClientTask
    : public WebSocketTaskBase,
      public std::enable_shared_from_this<WebSocketClientTask>,
      public NonCopyableNonMovable<WebSocketClientTask> {
 public:
  using Executor = HttpClientTask::Executor;
  using HttpDoneCallback = HttpClientTask::Callback;
  enum class LifecycleState {
    kInit,
    kStarting,
    kAcquiring,
    kTlsHandshaking,
    kWsHandshaking,
    kOpen,
    kClosing,
    kClosed,
  };
  enum class CloseReason { kNone, kUserCancel, kRemoteClose, kError };

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

  /**
   * @brief Create WebSocket task from an already connected TCP stream.
   *
   * The passed stream is moved into the task and consumed by Start().
   */
  static std::shared_ptr<WebSocketClientTask> CreateHttpRaw(
      Executor io_executor, TcpStream stream, std::string host,
      std::string target, HandlerPtr handler, HttpClientOptions options = {});

  /**
   * @brief Create secure WebSocket task from an already connected and
   * handshaked SSL stream.
   *
   * The passed stream is moved into the task and consumed by Start().
   */
  static std::shared_ptr<WebSocketClientTask> CreateHttpsRaw(
      Executor io_executor, SslStream stream, std::string host,
      std::string target, HandlerPtr handler, HttpClientOptions options = {});

  std::shared_ptr<WebSocketClientTask> OnHttpDone(HttpDoneCallback cb);

  HttpClientRequest& Request() noexcept;

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

  friend class HttpClientSession;

 private:
  void DoStart();
  void DoAcquire();
  void OnAcquireComplete(boost::system::error_code ec, StreamSlot slot);
  void CreateWebSocketStream(TcpStream stream);
  void CreateSecureWebSocketStream(SslStream stream);
  /**
   * @brief Perform deferred TLS handshake for WSS connections.
   *
   * Call chain: OnAcquireComplete → DoDeferredTlsHandshake
   *   → ssl_stream.async_handshake → OnDeferredTlsHandshakeComplete
   *   → CreateSecureWebSocketStream → StartWebSocketHandshake
   *
   * WebSocketStreamBuilder intentionally returns only a connected TcpStream
   * for WSS so this task can control the WSS transition. This method stores
   * a temporary SslStream in pending_transport_ for the async TLS handshake,
   * then moves the handshaked stream into websocket_stream_ as the final
   * SecureWebSocketStream before starting the WebSocket handshake.
   */
  void DoDeferredTlsHandshake(TcpStream tcp_stream, SslContextPtr ssl_ctx,
                              bool verify_peer);
  void OnDeferredTlsHandshakeComplete(boost::system::error_code ec);
  void StartWebSocketHandshake();
  void OnWebSocketHandshakeCompleted(
      boost::system::error_code ec,
      std::shared_ptr<WebSocketResponse> response);
  void SetCreateError(boost::system::error_code ec, std::string message);

  /** @brief Set assembler and builder for assembled mode. */
  void SetAssembler(std::shared_ptr<RequestAssembler> assembler,
                    std::shared_ptr<StreamBuilder> builder);

  /** @brief Replace only the assembler, keeping the existing builder. */
  void SetAssemblerOnly(std::shared_ptr<RequestAssembler> assembler);

  bool WritePingControl(std::string payload);
  bool WritePongControl(std::string payload);
  bool WriteCloseControl();
  void BeginReadLoop();
  /**
   * @brief Handle read completion for both ws and wss streams.
   *
   * Call chain: BeginReadLoop → async_read → OnWebSocketRead
   *
   * Processes close frames, errors, and dispatches received messages.
   * Re-enters BeginReadLoop on success.
   */
  void OnWebSocketRead(boost::system::error_code ec, bool binary,
                       std::string payload);
  void NotifyCloseOnce(boost::system::error_code ec);
  void FailAndClose(boost::system::error_code ec, std::string message);
  void EmitHttpDone(boost::system::error_code ec,
                    HttpResponseHeader header = {},
                    HttpClientResponse response = {});
  [[nodiscard]] bool IsOpen() const noexcept;
  [[nodiscard]] bool IsClosingOrClosed() const noexcept;

  Executor io_executor_;
  std::string host_;
  std::string port_;
  std::string target_;
  HttpClientOptions options_;
  bool use_ssl_{false};
  SslContextPtr ssl_ctx_;
  HttpClientRequest request_;
  /**
   * @brief Final WebSocket transport used after WS/WSS setup.
   *
   * This owns websocket::stream<TcpStream> for WS or
   * websocket::stream<SslStream> for WSS. Runtime reads, writes, ping/pong,
   * and close operations use this member.
   */
  ClientWebSocketStream websocket_stream_;
  /**
   * @brief Temporary WSS TLS transport during deferred handshake.
   *
   * This is not a second live WebSocket stream. It replaces the pre-refactor
   * heap-owned temporary SslStream used only to keep the TLS stream alive
   * across async_handshake(). On success it is moved into websocket_stream_;
   * on cancellation or failure it is closed/reset.
   */
  ClientStream pending_transport_;
  FlatBuffer ws_read_buffer_;
  HttpDoneCallback on_http_done_;
  LifecycleState lifecycle_state_{LifecycleState::kInit};
  CloseReason close_reason_{CloseReason::kNone};
  std::optional<boost::system::error_code> create_error_;
  std::string create_error_message_;

  // Assembler + builder (nullptr for raw mode).
  std::shared_ptr<RequestAssembler> assembler_;
  std::shared_ptr<StreamBuilder> builder_;
  ConnectionKey connection_key_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_WEBSOCKET_CLIENT_TASK_H_
