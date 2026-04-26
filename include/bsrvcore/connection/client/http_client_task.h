/**
 * @file http_client_task.h
 * @brief Asynchronous HTTP/HTTPS client task with staged callbacks.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CONNECTION_CLIENT_HTTP_CLIENT_TASK_H_
#define BSRVCORE_CONNECTION_CLIENT_HTTP_CLIENT_TASK_H_

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>  // NOLINT(misc-include-cleaner): Boost.Beast http headers require std::uint32_t on some toolchains.
#include <functional>
#include <memory>
#include <string>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"

namespace bsrvcore {

class HttpClientSession;
class SessionRequestAssembler;

/**
 * @brief Alias of request message type used by HttpClientTask.
 */
using HttpClientRequest = HttpRequest;

/**
 * @brief Alias of response message type used by HttpClientTask.
 */
using HttpClientResponse = HttpResponse;

/**
 * @brief Runtime options for a single HTTP/HTTPS client request.
 */
struct HttpClientOptions : public CopyableMovable<HttpClientOptions> {
  /** @brief DNS resolve timeout. */
  std::chrono::milliseconds resolve_timeout{2000};
  /** @brief TCP connect timeout. */
  std::chrono::milliseconds connect_timeout{2000};
  /** @brief TLS handshake timeout for HTTPS. */
  std::chrono::milliseconds tls_handshake_timeout{2000};
  /** @brief HTTP write timeout for sending request bytes. */
  std::chrono::milliseconds write_timeout{2000};
  /** @brief Timeout for reading the response header. */
  std::chrono::milliseconds read_header_timeout{2000};
  /** @brief Timeout for reading response body bytes. */
  std::chrono::milliseconds read_body_timeout{5000};
  /** @brief Maximum response body size accepted by parser. */
  std::size_t max_response_body_bytes{
      static_cast<std::size_t>(4 * 1024 * 1024)};
  /** @brief Enable TLS peer and host verification for HTTPS. */
  bool verify_peer{true};
  /** @brief Keep-alive preference sent in request and used by read flow. */
  bool keep_alive{false};
  /** @brief Optional User-Agent header value. */
  std::string user_agent;
};

/**
 * @brief Stage of callback delivery for HttpClientTask.
 */
enum class HttpClientStage {
  /** @brief Connection established (and TLS handshake completed for HTTPS). */
  kConnected,
  /** @brief Response header received. */
  kHeader,
  /** @brief Response body chunk received (only when OnChunk is registered). */
  kChunk,
  /** @brief Final completion stage (success, failure, or cancellation). */
  kDone,
};

/**
 * @brief Internal pipeline location where a failure occurred.
 */
enum class HttpClientErrorStage {
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
  /** @brief Response header read failed. */
  kReadHeader,
  /** @brief Response body read failed. */
  kReadBody,
};

/**
 * @brief Unified callback payload for all HttpClientTask stages.
 */
struct HttpClientResult : public CopyableMovable<HttpClientResult> {
  /** @brief Operation error code, 0 on success. */
  boost::system::error_code ec;
  /** @brief Current callback stage. */
  HttpClientStage stage{HttpClientStage::kDone};
  /** @brief Failure location in transport pipeline. */
  HttpClientErrorStage error_stage{HttpClientErrorStage::kNone};
  /** @brief True when completion was driven by Cancel(). */
  bool cancelled{false};
  /** @brief Response header snapshot (valid from kHeader/kDone). */
  HttpResponseHeader header;
  /** @brief Final response (mainly meaningful at kDone success). */
  HttpClientResponse response;
  /** @brief Incremental body bytes for kChunk callbacks. */
  std::string chunk;

  /**
   * @brief Parse response body as a JSON value.
   * @param out Parsed JSON output.
   * @return Parse error code, or success when parsing completed.
   */
  [[nodiscard]] JsonErrorCode ParseJsonBody(JsonValue& out) const;

  /**
   * @brief Parse response body as a JSON object.
   * @param out Parsed JSON object output.
   * @return Parse/type error code, or success when parsing completed.
   */
  [[nodiscard]] JsonErrorCode ParseJsonBody(JsonObject& out) const;

  /**
   * @brief Parse response body as a JSON value.
   * @param out Parsed JSON output.
   * @return True on success.
   */
  [[nodiscard]] bool TryParseJsonBody(JsonValue& out) const;

  /**
   * @brief Parse response body as a JSON object.
   * @param out Parsed JSON object output.
   * @return True on success.
   */
  [[nodiscard]] bool TryParseJsonBody(JsonObject& out) const;
};

/**
 * @brief Asynchronous HTTP/HTTPS client task with staged callbacks.
 *
 * The task exposes stage callbacks (`OnConnected`, `OnHeader`, `OnChunk`,
 * `OnDone`) and expresses all statuses through `HttpClientResult` fields.
 */
class HttpClientTask : public std::enable_shared_from_this<HttpClientTask>,
                       public NonCopyableNonMovable<HttpClientTask> {
 public:
  /** @brief Executor type accepted by HttpClientTask factories. */
  using Executor = IoContextExecutor;
  /** @brief Callback type used by all stages. */
  using Callback = std::function<void(const HttpClientResult&)>;

  friend class HttpClientSession;

  /**
   * @brief Create plain HTTP task from host/port/target.
   */
  static std::shared_ptr<HttpClientTask> CreateHttp(
      Executor io_executor, std::string host, std::string port,
      std::string target, HttpVerb method, HttpClientOptions options = {});
  /** @brief Create plain HTTP task with a dedicated callback executor. */
  static std::shared_ptr<HttpClientTask> CreateHttp(
      Executor io_executor, Executor callback_executor, std::string host,
      std::string port, std::string target, HttpVerb method,
      HttpClientOptions options = {});

  /**
   * @brief Create HTTPS task from host/port/target.
   */
  static std::shared_ptr<HttpClientTask> CreateHttps(
      Executor io_executor, std::string host, std::string port,
      std::string target, HttpVerb method, HttpClientOptions options = {});
  /** @brief Create HTTPS task with a dedicated callback executor. */
  static std::shared_ptr<HttpClientTask> CreateHttps(
      Executor io_executor, Executor callback_executor, std::string host,
      std::string port, std::string target, HttpVerb method,
      HttpClientOptions options = {});
  /**
   * @brief Create HTTPS task from host/port/target with caller-provided SSL
   * context.
   */
  static std::shared_ptr<HttpClientTask> CreateHttps(
      Executor io_executor, SslContextPtr ssl_ctx, std::string host,
      std::string port, std::string target, HttpVerb method,
      HttpClientOptions options = {});
  /** @brief Create HTTPS task with a dedicated callback executor and SSL
   * context. */
  static std::shared_ptr<HttpClientTask> CreateHttps(
      Executor io_executor, Executor callback_executor, SslContextPtr ssl_ctx,
      std::string host, std::string port, std::string target, HttpVerb method,
      HttpClientOptions options = {});

  /**
   * @brief Create task from URL without SSL context.
   *
   * Supports both `http://` and `https://`. HTTPS URLs allocate an internal
   * client SSL context and load system default trust roots.
   */
  static std::shared_ptr<HttpClientTask> CreateFromUrl(
      Executor io_executor, const std::string& url, HttpVerb method,
      HttpClientOptions options = {});
  /** @brief Create task from URL with a dedicated callback executor. */
  static std::shared_ptr<HttpClientTask> CreateFromUrl(
      Executor io_executor, Executor callback_executor, const std::string& url,
      HttpVerb method, HttpClientOptions options = {});

  /**
   * @brief Create task from URL with SSL context.
   */
  static std::shared_ptr<HttpClientTask> CreateFromUrl(
      Executor io_executor, SslContextPtr ssl_ctx, const std::string& url,
      HttpVerb method, HttpClientOptions options = {});
  /** @brief Create HTTPS task from URL with a dedicated callback executor. */
  static std::shared_ptr<HttpClientTask> CreateFromUrl(
      Executor io_executor, Executor callback_executor, SslContextPtr ssl_ctx,
      const std::string& url, HttpVerb method, HttpClientOptions options = {});

  /**
   * @brief Create HTTP task from an already connected TCP stream.
   *
   * The passed stream is moved into the task and consumed by Start().
   */
  static std::shared_ptr<HttpClientTask> CreateHttpRaw(
      Executor io_executor, TcpStream stream, std::string host,
      std::string target, HttpVerb method, HttpClientOptions options = {});
  /** @brief Create HTTP raw task with a dedicated callback executor. */
  static std::shared_ptr<HttpClientTask> CreateHttpRaw(
      Executor io_executor, Executor callback_executor, TcpStream stream,
      std::string host, std::string target, HttpVerb method,
      HttpClientOptions options = {});

  /**
   * @brief Create HTTPS task from an already connected and handshaked SSL
   * stream.
   *
   * The passed stream is moved into the task and consumed by Start().
   */
  static std::shared_ptr<HttpClientTask> CreateHttpsRaw(
      Executor io_executor, SslStream stream, std::string host,
      std::string target, HttpVerb method, HttpClientOptions options = {});
  /** @brief Create HTTPS raw task with a dedicated callback executor. */
  static std::shared_ptr<HttpClientTask> CreateHttpsRaw(
      Executor io_executor, Executor callback_executor, SslStream stream,
      std::string host, std::string target, HttpVerb method,
      HttpClientOptions options = {});

  /** @brief Register connected-stage callback. */
  std::shared_ptr<HttpClientTask> OnConnected(Callback cb);
  /** @brief Register header-stage callback. */
  std::shared_ptr<HttpClientTask> OnHeader(Callback cb);
  /** @brief Register chunk-stage callback. */
  std::shared_ptr<HttpClientTask> OnChunk(Callback cb);
  /** @brief Register done-stage callback (final convergence point). */
  std::shared_ptr<HttpClientTask> OnDone(Callback cb);

  /**
   * @brief Access mutable request before Start().
   */
  HttpClientRequest& Request() noexcept;

  /**
   * @brief Replace request body with serialized JSON.
   * @param value JSON value to serialize.
   */
  void SetJson(const JsonValue& value);

  /**
   * @brief Replace request body with serialized JSON.
   * @param value JSON value to serialize.
   */
  void SetJson(JsonValue&& value);

  /**
   * @brief Start asynchronous execution.
   */
  void Start();
  /**
   * @brief Request cancellation and close underlying transport.
   */
  void Cancel();

  /** @brief Whether the latest terminal state is a failure. */
  bool Failed() const noexcept;
  /** @brief Latest failure code, if any. */
  boost::system::error_code ErrorCode() const noexcept;
  /** @brief Latest failure stage, if any. */
  HttpClientErrorStage ErrorStage() const noexcept;

  ~HttpClientTask();

 private:
  class Impl;

  static std::shared_ptr<Impl> CreateDefaultHttpsImpl(
      Executor io_executor, Executor callback_executor, std::string host,
      std::string port, std::string target, HttpVerb method,
      HttpClientOptions options);

  explicit HttpClientTask(std::shared_ptr<Impl> impl);
  static std::shared_ptr<HttpClientTask> CreateTask(std::shared_ptr<Impl> impl);

  /** @brief Internal helper: create an assembled-mode task. */
  static std::shared_ptr<HttpClientTask> CreateAssembledTask(
      Executor io_executor, Executor callback_executor, std::string scheme,
      std::string host, std::string port, std::string target, HttpVerb method,
      HttpClientOptions options, SslContextPtr ssl_ctx,
      boost::system::error_code create_ec = {});

  /** @brief Internal helper: create a session-backed assembled task. */
  static std::shared_ptr<HttpClientTask> CreateSessionTask(
      std::shared_ptr<SessionRequestAssembler> assembler, Executor io_executor,
      Executor callback_executor, std::string scheme, std::string host,
      std::string port, std::string target, HttpVerb method,
      HttpClientOptions options, SslContextPtr ssl_ctx,
      boost::system::error_code create_ec = {});

  std::shared_ptr<Impl> impl_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_HTTP_CLIENT_TASK_H_
