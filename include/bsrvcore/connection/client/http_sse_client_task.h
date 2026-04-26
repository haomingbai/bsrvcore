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

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <cstdint>  // NOLINT(misc-include-cleaner): Boost.Beast http headers require std::uint32_t on some toolchains.
#include <functional>
#include <memory>
#include <string>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore {

/**
 * @brief Runtime options for a single SSE connection.
 */
struct HttpSseClientOptions : public CopyableMovable<HttpSseClientOptions> {
  /** @brief DNS resolve timeout. */
  std::chrono::milliseconds resolve_timeout{2000};
  /** @brief TCP connect timeout. */
  std::chrono::milliseconds connect_timeout{2000};
  /** @brief TLS handshake timeout for HTTPS SSE endpoints. */
  std::chrono::milliseconds tls_handshake_timeout{2000};
  /** @brief Request write timeout. */
  std::chrono::milliseconds write_timeout{2000};
  /** @brief Header read timeout for initial response. */
  std::chrono::milliseconds read_header_timeout{2000};
  /** @brief Timeout for each Next() read operation. */
  std::chrono::milliseconds read_body_timeout{10000};
  /** @brief Enable TLS peer and host verification. */
  bool verify_peer{true};
  /** @brief Keep-alive preference for the SSE request. */
  bool keep_alive{true};
  /** @brief Optional User-Agent header value. */
  std::string user_agent;
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

  /** @brief Create plain HTTP SSE task from host/port/target. */
  static std::shared_ptr<HttpSseClientTask> CreateHttp(
      Executor io_executor, std::string host, std::string port,
      std::string target, HttpSseClientOptions options = {});
  /** @brief Create plain HTTP SSE task with a dedicated callback executor. */
  static std::shared_ptr<HttpSseClientTask> CreateHttp(
      Executor io_executor, Executor callback_executor, std::string host,
      std::string port, std::string target, HttpSseClientOptions options = {});

  /** @brief Create HTTPS SSE task from host/port/target. */
  static std::shared_ptr<HttpSseClientTask> CreateHttps(
      Executor io_executor, std::string host, std::string port,
      std::string target, HttpSseClientOptions options = {});
  /** @brief Create HTTPS SSE task with a dedicated callback executor. */
  static std::shared_ptr<HttpSseClientTask> CreateHttps(
      Executor io_executor, Executor callback_executor, std::string host,
      std::string port, std::string target, HttpSseClientOptions options = {});
  /** @brief Create HTTPS SSE task from host/port/target with caller-provided
   * SSL context. */
  static std::shared_ptr<HttpSseClientTask> CreateHttps(
      Executor io_executor, SslContextPtr ssl_ctx, std::string host,
      std::string port, std::string target, HttpSseClientOptions options = {});
  /** @brief Create HTTPS SSE task with a dedicated callback executor. */
  static std::shared_ptr<HttpSseClientTask> CreateHttps(
      Executor io_executor, Executor callback_executor, SslContextPtr ssl_ctx,
      std::string host, std::string port, std::string target,
      HttpSseClientOptions options = {});

  /**
   * @brief Create SSE task from URL without SSL context.
   *
   * Supports both `http://` and `https://`. HTTPS URLs allocate an internal
   * client SSL context and load system default trust roots.
   */
  static std::shared_ptr<HttpSseClientTask> CreateFromUrl(
      Executor io_executor, const std::string& url,
      HttpSseClientOptions options = {});
  /** @brief Create SSE task from URL with a dedicated callback executor. */
  static std::shared_ptr<HttpSseClientTask> CreateFromUrl(
      Executor io_executor, Executor callback_executor, const std::string& url,
      HttpSseClientOptions options = {});

  /** @brief Create SSE task from URL with SSL context. */
  static std::shared_ptr<HttpSseClientTask> CreateFromUrl(
      Executor io_executor, SslContextPtr ssl_ctx, const std::string& url,
      HttpSseClientOptions options = {});
  /** @brief Create SSE task from URL with SSL and a dedicated callback
   * executor. */
  static std::shared_ptr<HttpSseClientTask> CreateFromUrl(
      Executor io_executor, Executor callback_executor, SslContextPtr ssl_ctx,
      const std::string& url, HttpSseClientOptions options = {});

  /**
   * @brief Create HTTP SSE task from an already connected TCP stream.
   *
   * The passed stream is moved into the task and consumed by Start().
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttpRaw(
      Executor io_executor, TcpStream stream, std::string host,
      std::string target, HttpSseClientOptions options = {});
  /** @brief Create HTTP SSE raw task with a dedicated callback executor. */
  static std::shared_ptr<HttpSseClientTask> CreateHttpRaw(
      Executor io_executor, Executor callback_executor, TcpStream stream,
      std::string host, std::string target, HttpSseClientOptions options = {});

  /**
   * @brief Create HTTPS SSE task from an already connected and handshaked SSL
   * stream.
   *
   * The passed stream is moved into the task and consumed by Start().
   */
  static std::shared_ptr<HttpSseClientTask> CreateHttpsRaw(
      Executor io_executor, SslStream stream, std::string host,
      std::string target, HttpSseClientOptions options = {});
  /** @brief Create HTTPS SSE raw task with a dedicated callback executor. */
  static std::shared_ptr<HttpSseClientTask> CreateHttpsRaw(
      Executor io_executor, Executor callback_executor, SslStream stream,
      std::string host, std::string target, HttpSseClientOptions options = {});

  /** @brief Access mutable request before Start(). */
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

  /** @brief Whether the latest terminal state is a failure. */
  bool Failed() const noexcept;
  /** @brief Latest failure code, if any. */
  boost::system::error_code ErrorCode() const noexcept;
  /** @brief Latest failure stage, if any. */
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
