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

#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/system/errc.hpp>
#include <memory>
#include <optional>
#include <string>

#include "bsrvcore/connection/client/client_stream.h"
#include "bsrvcore/connection/client/client_websocket_stream.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/stream_slot.h"
#include "bsrvcore/connection/websocket/websocket_task_base.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"

// Forward declarations for assembled mode.
namespace bsrvcore {
class RequestAssembler;
class StreamBuilder;
}  // namespace bsrvcore

namespace bsrvcore {

/**
 * @brief Client-side WebSocket task for ws/wss connections.
 *
 * The task owns the HTTP upgrade request, drives connection acquisition and
 * WebSocket handshake, then exposes message and control writes through
 * WebSocketTaskBase.
 */
class WebSocketClientTask
    : public WebSocketTaskBase,
      public std::enable_shared_from_this<WebSocketClientTask>,
      public NonCopyableNonMovable<WebSocketClientTask> {
 public:
  /** @brief Executor type used for asynchronous client operations. */
  using Executor = HttpClientTask::Executor;
  /** @brief Callback type invoked when the HTTP upgrade phase completes. */
  using HttpDoneCallback = HttpClientTask::Callback;

  /** @brief Internal lifecycle state for startup, open, and close handling. */
  enum class LifecycleState {
    /** @brief Constructed but not started. */
    kInit,
    /** @brief Start has been requested. */
    kStarting,
    /** @brief Waiting for a stream from the stream builder/session. */
    kAcquiring,
    /** @brief Performing deferred TLS handshake for WSS. */
    kTlsHandshaking,
    /** @brief Performing WebSocket HTTP upgrade handshake. */
    kWsHandshaking,
    /** @brief WebSocket is open for reads and writes. */
    kOpen,
    /** @brief Close has started but completion is pending. */
    kClosing,
    /** @brief Task is closed. */
    kClosed,
  };

  /** @brief Reason the task entered closing/closed state. */
  enum class CloseReason {
    /** @brief No close reason has been recorded. */
    kNone,
    /** @brief Caller cancelled the task. */
    kUserCancel,
    /** @brief Peer initiated or completed the close. */
    kRemoteClose,
    /** @brief Transport or protocol error closed the task. */
    kError
  };

  /**
   * @brief Create a plaintext ws task for host/port/target.
   *
   * @param io_executor Executor used for network I/O.
   * @param host Target host name.
   * @param port Target service port.
   * @param target WebSocket request target.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task.
   */
  static std::shared_ptr<WebSocketClientTask> CreateHttp(
      Executor io_executor, std::string host, std::string port,
      std::string target, HandlerPtr handler, HttpClientOptions options = {});

  /**
   * @brief Create a wss task using a default SSL context.
   *
   * @param io_executor Executor used for network I/O.
   * @param host Target host name.
   * @param port Target service port.
   * @param target WebSocket request target.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task.
   */
  static std::shared_ptr<WebSocketClientTask> CreateHttps(
      Executor io_executor, std::string host, std::string port,
      std::string target, HandlerPtr handler, HttpClientOptions options = {});

  /**
   * @brief Create a wss task using a caller-supplied SSL context.
   *
   * @param io_executor Executor used for network I/O.
   * @param ssl_ctx TLS context to use for the WSS connection.
   * @param host Target host name.
   * @param port Target service port.
   * @param target WebSocket request target.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task.
   */
  static std::shared_ptr<WebSocketClientTask> CreateHttps(
      Executor io_executor, SslContextPtr ssl_ctx, std::string host,
      std::string port, std::string target, HandlerPtr handler,
      HttpClientOptions options = {});

  /**
   * @brief Create a ws/wss task from a URL using default SSL handling.
   *
   * @param io_executor Executor used for network I/O.
   * @param url Absolute `ws://` or `wss://` URL.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task.
   */
  static std::shared_ptr<WebSocketClientTask> CreateFromUrl(
      Executor io_executor, std::string url, HandlerPtr handler,
      HttpClientOptions options = {});

  /**
   * @brief Create a ws/wss task from a URL with an explicit SSL context.
   *
   * @param io_executor Executor used for network I/O.
   * @param ssl_ctx TLS context used when `url` is WSS.
   * @param url Absolute `ws://` or `wss://` URL.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task.
   */
  static std::shared_ptr<WebSocketClientTask> CreateFromUrl(
      Executor io_executor, SslContextPtr ssl_ctx, std::string url,
      HandlerPtr handler, HttpClientOptions options = {});

  /**
   * @brief Create WebSocket task from an already connected TCP stream.
   *
   * The passed stream is moved into the task and consumed by Start().
   *
   * @param io_executor Executor used for network I/O.
   * @param stream Connected TCP stream to move into the task.
   * @param host Target host name used for request headers.
   * @param target WebSocket request target.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task.
   */
  static std::shared_ptr<WebSocketClientTask> CreateHttpRaw(
      Executor io_executor, TcpStream stream, std::string host,
      std::string target, HandlerPtr handler, HttpClientOptions options = {});

  /**
   * @brief Create secure WebSocket task from an already connected and
   * handshaked SSL stream.
   *
   * The passed stream is moved into the task and consumed by Start().
   *
   * @param io_executor Executor used for network I/O.
   * @param stream Connected and handshaked SSL stream to move into the task.
   * @param host Target host name used for request headers.
   * @param target WebSocket request target.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task.
   */
  static std::shared_ptr<WebSocketClientTask> CreateHttpsRaw(
      Executor io_executor, SslStream stream, std::string host,
      std::string target, HandlerPtr handler, HttpClientOptions options = {});

  /**
   * @brief Register a callback for the HTTP upgrade completion.
   *
   * @param cb Callback invoked when the HTTP upgrade phase finishes.
   * @return Shared task pointer for fluent callback registration.
   */
  std::shared_ptr<WebSocketClientTask> OnHttpDone(HttpDoneCallback cb);

  /**
   * @brief Return the mutable HTTP upgrade request.
   *
   * @return Mutable HTTP upgrade request object.
   */
  HttpClientRequest& Request() noexcept;

  /** @brief Start connection acquisition and WebSocket handshake. */
  void Start() override;
  /** @brief Cancel the task and close the transport. */
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
  ~WebSocketClientTask() override;

  /**
   * @brief Construct a task; prefer static Create* helpers for callers.
   *
   * @param io_executor Executor used for network I/O.
   * @param host Target host name.
   * @param port Target service port.
   * @param target WebSocket request target.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @param use_ssl Whether the connection should use WSS.
   * @param ssl_ctx Optional TLS context for WSS.
   */
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
