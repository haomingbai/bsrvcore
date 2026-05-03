/**
 * @file http_client_session.h
 * @brief HttpClientSession provides cookie-managed factories for
 * HttpClientTask.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_HTTP_CLIENT_SESSION_H_
#define BSRVCORE_CONNECTION_CLIENT_HTTP_CLIENT_SESSION_H_

#include <memory>
#include <string>

#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/websocket_client_task.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"

namespace bsrvcore {

/**
 * @brief A non-persistent, in-memory client session.
 *
 * HttpClientSession inherits from SessionRequestAssembler and acts as a
 * factory for HttpClientTask with per-session cookie management.
 *
 * - Backward compatibility: HttpClientTask's static Create* factories remain
 *   available and create lightweight tasks without a session.
 * - When tasks are created from HttpClientSession, the task uses the session
 *   as its RequestAssembler (cookie injection + Set-Cookie sync) and a
 *   DirectStreamBuilder for connection acquisition.
 *
 * Note: this session does NOT provide persistence.
 */
class HttpClientSession
    : public SessionRequestAssembler,
      public std::enable_shared_from_this<HttpClientSession>,
      public NonCopyableNonMovable<HttpClientSession> {
 public:
  /**
   * @brief Create a shared session instance.
   *
   * @return New in-memory HTTP client session.
   */
  static std::shared_ptr<HttpClientSession> Create();

  /**
   * @brief Create plain HTTP task from host/port/target.
   *
   * @param io_executor Executor used for network I/O.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param method HTTP request method.
   * @param options Per-request client options.
   * @return Newly created unstarted task using this session.
   */
  std::shared_ptr<HttpClientTask> CreateHttp(
      HttpClientTask::Executor io_executor, std::string host, std::string port,
      std::string target, HttpVerb method, HttpClientOptions options = {});
  /**
   * @brief Create plain HTTP task with a dedicated callback executor.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param method HTTP request method.
   * @param options Per-request client options.
   * @return Newly created unstarted task using this session.
   */
  std::shared_ptr<HttpClientTask> CreateHttp(
      HttpClientTask::Executor io_executor,
      HttpClientTask::Executor callback_executor, std::string host,
      std::string port, std::string target, HttpVerb method,
      HttpClientOptions options = {});

  /**
   * @brief Create HTTPS task from host/port/target.
   *
   * @param io_executor Executor used for network I/O.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param method HTTP request method.
   * @param options Per-request client options.
   * @return Newly created unstarted task using this session.
   */
  std::shared_ptr<HttpClientTask> CreateHttps(
      HttpClientTask::Executor io_executor, std::string host, std::string port,
      std::string target, HttpVerb method, HttpClientOptions options = {});
  /**
   * @brief Create HTTPS task with a dedicated callback executor.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param method HTTP request method.
   * @param options Per-request client options.
   * @return Newly created unstarted task using this session.
   */
  std::shared_ptr<HttpClientTask> CreateHttps(
      HttpClientTask::Executor io_executor,
      HttpClientTask::Executor callback_executor, std::string host,
      std::string port, std::string target, HttpVerb method,
      HttpClientOptions options = {});
  /**
   * @brief Create HTTPS task from host/port/target with caller-provided SSL
   * context.
   *
   * @param io_executor Executor used for network I/O.
   * @param ssl_ctx TLS context to use for the HTTPS connection.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param method HTTP request method.
   * @param options Per-request client options.
   * @return Newly created unstarted task using this session.
   */
  std::shared_ptr<HttpClientTask> CreateHttps(
      HttpClientTask::Executor io_executor, SslContextPtr ssl_ctx,
      std::string host, std::string port, std::string target, HttpVerb method,
      HttpClientOptions options = {});
  /**
   * @brief Create HTTPS task with a dedicated callback executor and SSL
   * context.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param ssl_ctx TLS context to use for the HTTPS connection.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param method HTTP request method.
   * @param options Per-request client options.
   * @return Newly created unstarted task using this session.
   */
  std::shared_ptr<HttpClientTask> CreateHttps(
      HttpClientTask::Executor io_executor,
      HttpClientTask::Executor callback_executor, SslContextPtr ssl_ctx,
      std::string host, std::string port, std::string target, HttpVerb method,
      HttpClientOptions options = {});

  /**
   * @brief Create task from URL without SSL context.
   *
   * @param io_executor Executor used for network I/O.
   * @param url Absolute `http://` or `https://` URL.
   * @param method HTTP request method.
   * @param options Per-request client options.
   * @return Newly created unstarted task using this session.
   */
  std::shared_ptr<HttpClientTask> CreateFromUrl(
      HttpClientTask::Executor io_executor, std::string url, HttpVerb method,
      HttpClientOptions options = {});
  /**
   * @brief Create task from URL with a dedicated callback executor.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param url Absolute `http://` or `https://` URL.
   * @param method HTTP request method.
   * @param options Per-request client options.
   * @return Newly created unstarted task using this session.
   */
  std::shared_ptr<HttpClientTask> CreateFromUrl(
      HttpClientTask::Executor io_executor,
      HttpClientTask::Executor callback_executor, std::string url,
      HttpVerb method, HttpClientOptions options = {});

  /**
   * @brief Create task from URL with SSL context.
   *
   * @param io_executor Executor used for network I/O.
   * @param ssl_ctx TLS context used when `url` is HTTPS.
   * @param url Absolute `http://` or `https://` URL.
   * @param method HTTP request method.
   * @param options Per-request client options.
   * @return Newly created unstarted task using this session.
   */
  std::shared_ptr<HttpClientTask> CreateFromUrl(
      HttpClientTask::Executor io_executor, SslContextPtr ssl_ctx,
      std::string url, HttpVerb method, HttpClientOptions options = {});
  /**
   * @brief Create task from URL with SSL and a dedicated callback executor.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param ssl_ctx TLS context used when `url` is HTTPS.
   * @param url Absolute `http://` or `https://` URL.
   * @param method HTTP request method.
   * @param options Per-request client options.
   * @return Newly created unstarted task using this session.
   */
  std::shared_ptr<HttpClientTask> CreateFromUrl(
      HttpClientTask::Executor io_executor,
      HttpClientTask::Executor callback_executor, SslContextPtr ssl_ctx,
      std::string url, HttpVerb method, HttpClientOptions options = {});

  /**
   * @brief Create plain WebSocket task from host/port/target.
   *
   * @param io_executor Executor used for network I/O.
   * @param host Target host name.
   * @param port Target service port.
   * @param target WebSocket request target.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task using this session.
   */
  std::shared_ptr<WebSocketClientTask> CreateWebSocketHttp(
      HttpClientTask::Executor io_executor, std::string host, std::string port,
      std::string target, WebSocketClientTask::HandlerPtr handler,
      HttpClientOptions options = {});

  /**
   * @brief Create HTTPS WebSocket task from host/port/target.
   *
   * @param io_executor Executor used for network I/O.
   * @param host Target host name.
   * @param port Target service port.
   * @param target WebSocket request target.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task using this session.
   */
  std::shared_ptr<WebSocketClientTask> CreateWebSocketHttps(
      HttpClientTask::Executor io_executor, std::string host, std::string port,
      std::string target, WebSocketClientTask::HandlerPtr handler,
      HttpClientOptions options = {});

  /**
   * @brief Create HTTPS WebSocket task from host/port/target with
   * caller-provided SSL context.
   *
   * @param io_executor Executor used for network I/O.
   * @param ssl_ctx TLS context to use for the WSS connection.
   * @param host Target host name.
   * @param port Target service port.
   * @param target WebSocket request target.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task using this session.
   */
  std::shared_ptr<WebSocketClientTask> CreateWebSocketHttps(
      HttpClientTask::Executor io_executor, SslContextPtr ssl_ctx,
      std::string host, std::string port, std::string target,
      WebSocketClientTask::HandlerPtr handler, HttpClientOptions options = {});

  /**
   * @brief Create WebSocket task from URL without SSL context.
   *
   * @param io_executor Executor used for network I/O.
   * @param url Absolute `ws://` or `wss://` URL.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task using this session.
   */
  std::shared_ptr<WebSocketClientTask> CreateWebSocketFromUrl(
      HttpClientTask::Executor io_executor, std::string url,
      WebSocketClientTask::HandlerPtr handler, HttpClientOptions options = {});

  /**
   * @brief Create WebSocket task from URL with SSL context.
   *
   * @param io_executor Executor used for network I/O.
   * @param ssl_ctx TLS context used when `url` is WSS.
   * @param url Absolute `ws://` or `wss://` URL.
   * @param handler WebSocket event handler.
   * @param options Per-request client options.
   * @return Newly created unstarted WebSocket task using this session.
   */
  std::shared_ptr<WebSocketClientTask> CreateWebSocketFromUrl(
      HttpClientTask::Executor io_executor, SslContextPtr ssl_ctx,
      std::string url, WebSocketClientTask::HandlerPtr handler,
      HttpClientOptions options = {});

 private:
  HttpClientSession() = default;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_HTTP_CLIENT_SESSION_H_
