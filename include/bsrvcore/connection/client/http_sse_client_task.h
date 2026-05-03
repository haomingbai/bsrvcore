/**
 * @file http_sse_client_task.h
 * @brief Asynchronous SSE client task using Start/Next pull model.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_HTTP_SSE_CLIENT_TASK_H_
#define BSRVCORE_CONNECTION_CLIENT_HTTP_SSE_CLIENT_TASK_H_

#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/system/errc.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"

namespace bsrvcore {

/**
 * @brief Runtime options for a single SSE connection.
 *
 * Inherits from HttpClientOptions so that RequestAssembler::Assemble()
 * (which takes const HttpClientOptions&) can accept HttpSseClientOptions.
 * SSE-specific defaults are applied in the constructor.
 */
struct HttpSseClientOptions : public HttpClientOptions {
  HttpSseClientOptions() {
    // SSE-specific defaults that differ from HttpClientOptions.
    read_body_timeout = std::chrono::milliseconds{10000};
    keep_alive = true;
  }
};

/**
 * @brief Callback stage for SSE task pull model.
 */
enum class HttpSseClientStage {
  /** @brief Start() phase callback. */
  kStart,
  /** @brief Next() phase callback. */
  kNext,
};

/**
 * @brief Internal pipeline location where a failure occurred.
 */
enum class HttpSseClientErrorStage {
  /** @brief No failure. */
  kNone,
  /** @brief Invalid creation arguments or unsupported URL input. */
  kCreate,
  /** @brief DNS resolve failed. */
  kResolve,
  /** @brief TCP connect failed. */
  kConnect,
  /** @brief TLS handshake or SNI setup failed. */
  kTlsHandshake,
  /** @brief Request write failed. */
  kWriteRequest,
  /** @brief Response header read/validation failed. */
  kReadHeader,
  /** @brief Body chunk read failed. */
  kReadBody,
};

/**
 * @brief Unified callback payload for Start()/Next().
 */
struct HttpSseClientResult : public CopyableMovable<HttpSseClientResult> {
  /** @brief Operation error code, 0 on success. */
  boost::system::error_code ec;
  /** @brief Current callback stage. */
  HttpSseClientStage stage{HttpSseClientStage::kStart};
  /** @brief Failure location in transport pipeline. */
  HttpSseClientErrorStage error_stage{HttpSseClientErrorStage::kNone};
  /** @brief True when completion was driven by Cancel(). */
  bool cancelled{false};
  /** @brief True when stream reached normal EOF/remote close. */
  bool eof{false};
  /** @brief Response header snapshot populated by Start(). */
  HttpResponseHeader header;
  /** @brief Data chunk populated by Next() when available. */
  std::string chunk;
};

/**
 * @brief Asynchronous SSE client task using pull-based Start/Next model.
 */
class HttpSseClientTask
    : public std::enable_shared_from_this<HttpSseClientTask>,
      public NonCopyableNonMovable<HttpSseClientTask> {
 public:
  /** @brief Executor type accepted by HttpSseClientTask factories. */
  using Executor = IoContextExecutor;
  /** @brief Callback type for Start()/Next(). */
  using Callback = std::function<void(const HttpSseClientResult&)>;

  /**
   * @brief Create plain HTTP SSE task from host/port/target.
   *
   * @param io_executor Executor used for network I/O.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task.
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttp(
      Executor io_executor, std::string host, std::string port,
      std::string target, HttpSseClientOptions options = {});
  /**
   * @brief Create plain HTTP SSE task with a dedicated callback executor.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task.
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttp(
      Executor io_executor, Executor callback_executor, std::string host,
      std::string port, std::string target, HttpSseClientOptions options = {});

  /**
   * @brief Create HTTPS SSE task from host/port/target.
   *
   * @param io_executor Executor used for network I/O.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task.
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttps(
      Executor io_executor, std::string host, std::string port,
      std::string target, HttpSseClientOptions options = {});
  /**
   * @brief Create HTTPS SSE task with a dedicated callback executor.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task.
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttps(
      Executor io_executor, Executor callback_executor, std::string host,
      std::string port, std::string target, HttpSseClientOptions options = {});
  /**
   * @brief Create HTTPS SSE task from host/port/target with caller-provided
   * SSL context.
   *
   * @param io_executor Executor used for network I/O.
   * @param ssl_ctx TLS context to use for the HTTPS connection.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task.
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttps(
      Executor io_executor, SslContextPtr ssl_ctx, std::string host,
      std::string port, std::string target, HttpSseClientOptions options = {});
  /**
   * @brief Create HTTPS SSE task with a dedicated callback executor.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param ssl_ctx TLS context to use for the HTTPS connection.
   * @param host Target host name.
   * @param port Target service port.
   * @param target HTTP request target.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task.
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttps(
      Executor io_executor, Executor callback_executor, SslContextPtr ssl_ctx,
      std::string host, std::string port, std::string target,
      HttpSseClientOptions options = {});

  /**
   * @brief Create SSE task from URL without SSL context.
   *
   * Supports both `http://` and `https://`. HTTPS URLs allocate an internal
   * client SSL context and load system default trust roots.
   *
   * @param io_executor Executor used for network I/O.
   * @param url Absolute `http://` or `https://` URL.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task, or a task carrying a create
   * error.
   */
  static std::shared_ptr<HttpSseClientTask> CreateFromUrl(
      Executor io_executor, const std::string& url,
      HttpSseClientOptions options = {});
  /**
   * @brief Create SSE task from URL with a dedicated callback executor.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param url Absolute `http://` or `https://` URL.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task, or a task carrying a create
   * error.
   */
  static std::shared_ptr<HttpSseClientTask> CreateFromUrl(
      Executor io_executor, Executor callback_executor, const std::string& url,
      HttpSseClientOptions options = {});

  /**
   * @brief Create SSE task from URL with SSL context.
   *
   * @param io_executor Executor used for network I/O.
   * @param ssl_ctx TLS context used when `url` is HTTPS.
   * @param url Absolute `http://` or `https://` URL.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task, or a task carrying a create
   * error.
   */
  static std::shared_ptr<HttpSseClientTask> CreateFromUrl(
      Executor io_executor, SslContextPtr ssl_ctx, const std::string& url,
      HttpSseClientOptions options = {});
  /**
   * @brief Create SSE task from URL with SSL and a dedicated callback
   * executor.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param ssl_ctx TLS context used when `url` is HTTPS.
   * @param url Absolute `http://` or `https://` URL.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task, or a task carrying a create
   * error.
   */
  static std::shared_ptr<HttpSseClientTask> CreateFromUrl(
      Executor io_executor, Executor callback_executor, SslContextPtr ssl_ctx,
      const std::string& url, HttpSseClientOptions options = {});

  /**
   * @brief Create HTTP SSE task from an already connected TCP stream.
   *
   * The passed stream is moved into the task and consumed by Start().
   *
   * @param io_executor Executor used for network I/O.
   * @param stream Connected TCP stream to move into the task.
   * @param host Target host name used for request headers.
   * @param target HTTP request target.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task.
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttpRaw(
      Executor io_executor, TcpStream stream, std::string host,
      std::string target, HttpSseClientOptions options = {});
  /**
   * @brief Create HTTP SSE raw task with a dedicated callback executor.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param stream Connected TCP stream to move into the task.
   * @param host Target host name used for request headers.
   * @param target HTTP request target.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task.
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttpRaw(
      Executor io_executor, Executor callback_executor, TcpStream stream,
      std::string host, std::string target, HttpSseClientOptions options = {});

  /**
   * @brief Create HTTPS SSE task from an already connected and handshaked SSL
   * stream.
   *
   * The passed stream is moved into the task and consumed by Start().
   *
   * @param io_executor Executor used for network I/O.
   * @param stream Connected and handshaked SSL stream to move into the task.
   * @param host Target host name used for request headers.
   * @param target HTTP request target.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task.
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttpsRaw(
      Executor io_executor, SslStream stream, std::string host,
      std::string target, HttpSseClientOptions options = {});
  /**
   * @brief Create HTTPS SSE raw task with a dedicated callback executor.
   *
   * @param io_executor Executor used for network I/O.
   * @param callback_executor Executor used to deliver callbacks.
   * @param stream Connected and handshaked SSL stream to move into the task.
   * @param host Target host name used for request headers.
   * @param target HTTP request target.
   * @param options Per-request SSE client options.
   * @return Newly created unstarted SSE task.
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttpsRaw(
      Executor io_executor, Executor callback_executor, SslStream stream,
      std::string host, std::string target, HttpSseClientOptions options = {});

  /**
   * @brief Access mutable request before Start().
   *
   * @return Mutable request object owned by the task.
   */
  HttpRequest& Request() noexcept;

  /**
   * @brief Start connection and header handshake.
   * @param cb Callback invoked exactly once for Start stage.
   */
  void Start(Callback cb);
  /**
   * @brief Pull one read step from SSE body stream.
   * @param cb Callback invoked exactly once for this Next stage.
   */
  void Next(Callback cb);
  /** @brief Request cancellation and close underlying transport. */
  void Cancel();

  /**
   * @brief Whether the latest terminal state is a failure.
   *
   * @return True when the last terminal result has an error.
   */
  bool Failed() const noexcept;
  /**
   * @brief Latest failure code, if any.
   *
   * @return Stored error code, or success when none is recorded.
   */
  boost::system::error_code ErrorCode() const noexcept;
  /**
   * @brief Latest failure stage, if any.
   *
   * @return Pipeline stage where the last failure occurred.
   */
  HttpSseClientErrorStage ErrorStage() const noexcept;

  ~HttpSseClientTask();

 private:
  class Impl;

  explicit HttpSseClientTask(std::shared_ptr<Impl> impl);
  static std::shared_ptr<HttpSseClientTask> CreateTask(
      std::shared_ptr<Impl> impl);

  std::shared_ptr<Impl> impl_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_HTTP_SSE_CLIENT_TASK_H_
