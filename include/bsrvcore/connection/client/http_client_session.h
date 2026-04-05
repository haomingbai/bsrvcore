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

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http/verb.hpp>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/websocket_client_task.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore {

/**
 * @brief A non-persistent, in-memory client session.
 *
 * HttpClientSession acts as a factory for HttpClientTask and can manage
 * per-session state such as cookies.
 *
 * - Backward compatibility: HttpClientTask's static Create* factories remain
 *   available and create lightweight tasks without a session.
 * - When tasks are created from HttpClientSession, the task holds a weak
 *   reference back to the session and will:
 *   1) inject matching cookies before Start();
 *   2) synchronize response Set-Cookie entries back into the session.
 *
 * Note: this session does NOT provide persistence.
 */
class HttpClientSession
    : public std::enable_shared_from_this<HttpClientSession>,
      public NonCopyableNonMovable<HttpClientSession> {
 public:
  /** @brief Create a shared session instance. */
  static std::shared_ptr<HttpClientSession> Create();

  /** @brief Create plain HTTP task from host/port/target. */
  std::shared_ptr<HttpClientTask> CreateHttp(
      HttpClientTask::Executor io_executor, std::string host, std::string port,
      std::string target, boost::beast::http::verb method,
      HttpClientOptions options = {});
  /** @brief Create plain HTTP task with a dedicated callback executor. */
  std::shared_ptr<HttpClientTask> CreateHttp(
      HttpClientTask::Executor io_executor,
      HttpClientTask::Executor callback_executor, std::string host,
      std::string port, std::string target, boost::beast::http::verb method,
      HttpClientOptions options = {});

  /** @brief Create HTTPS task from host/port/target. */
  std::shared_ptr<HttpClientTask> CreateHttps(
      HttpClientTask::Executor io_executor, boost::asio::ssl::context& ssl_ctx,
      std::string host, std::string port, std::string target,
      boost::beast::http::verb method, HttpClientOptions options = {});
  /** @brief Create HTTPS task with a dedicated callback executor. */
  std::shared_ptr<HttpClientTask> CreateHttps(
      HttpClientTask::Executor io_executor,
      HttpClientTask::Executor callback_executor,
      boost::asio::ssl::context& ssl_ctx, std::string host, std::string port,
      std::string target, boost::beast::http::verb method,
      HttpClientOptions options = {});

  /** @brief Create task from URL without SSL context. */
  std::shared_ptr<HttpClientTask> CreateFromUrl(
      HttpClientTask::Executor io_executor, std::string url,
      boost::beast::http::verb method, HttpClientOptions options = {});
  /** @brief Create task from URL with a dedicated callback executor. */
  std::shared_ptr<HttpClientTask> CreateFromUrl(
      HttpClientTask::Executor io_executor,
      HttpClientTask::Executor callback_executor, std::string url,
      boost::beast::http::verb method, HttpClientOptions options = {});

  /** @brief Create task from URL with SSL context. */
  std::shared_ptr<HttpClientTask> CreateFromUrl(
      HttpClientTask::Executor io_executor, boost::asio::ssl::context& ssl_ctx,
      std::string url, boost::beast::http::verb method,
      HttpClientOptions options = {});
  /** @brief Create task from URL with SSL and a dedicated callback executor. */
  std::shared_ptr<HttpClientTask> CreateFromUrl(
      HttpClientTask::Executor io_executor,
      HttpClientTask::Executor callback_executor,
      boost::asio::ssl::context& ssl_ctx, std::string url,
      boost::beast::http::verb method, HttpClientOptions options = {});

  /** @brief Create plain WebSocket task from host/port/target. */
  std::shared_ptr<WebSocketClientTask> CreateWebSocketHttp(
      HttpClientTask::Executor io_executor, std::string host, std::string port,
      std::string target, WebSocketClientTask::HandlerPtr handler,
      HttpClientOptions options = {});

  /** @brief Create HTTPS WebSocket task from host/port/target. */
  std::shared_ptr<WebSocketClientTask> CreateWebSocketHttps(
      HttpClientTask::Executor io_executor, boost::asio::ssl::context& ssl_ctx,
      std::string host, std::string port, std::string target,
      WebSocketClientTask::HandlerPtr handler, HttpClientOptions options = {});

  /** @brief Create WebSocket task from URL without SSL context. */
  std::shared_ptr<WebSocketClientTask> CreateWebSocketFromUrl(
      HttpClientTask::Executor io_executor, std::string url,
      WebSocketClientTask::HandlerPtr handler, HttpClientOptions options = {});

  /** @brief Create WebSocket task from URL with SSL context. */
  std::shared_ptr<WebSocketClientTask> CreateWebSocketFromUrl(
      HttpClientTask::Executor io_executor, boost::asio::ssl::context& ssl_ctx,
      std::string url, WebSocketClientTask::HandlerPtr handler,
      HttpClientOptions options = {});

  /** @brief Remove all stored cookies. */
  void ClearCookies();

  /** @brief Current cookie count (after cleanup). */
  std::size_t CookieCount() const;

  // ---- Internal hooks used by HttpClientTask ----

  /**
   * @brief Inject cookies into request if Cookie header is absent.
   *
   * Respects user-specified Cookie header: if request already has Cookie,
   * this method does nothing.
   */
  void MaybeInjectCookies(HttpClientRequest& request, std::string_view host,
                          std::string_view target, bool is_https);

  /**
   * @brief Process one Set-Cookie header value and update the cookie jar.
   */
  void SyncSetCookie(std::string_view host, std::string_view target,
                     std::string_view set_cookie_value);

 private:
  struct Cookie;

  HttpClientSession() = default;

  void CleanupExpiredLocked() const;

  static std::string NormalizeHost(std::string_view host);
  static std::string NormalizeDomain(std::string_view domain);
  static std::string DefaultPathFromTarget(std::string_view target);
  static bool DomainMatches(std::string_view host, std::string_view domain);
  static bool PathMatches(std::string_view request_path,
                          std::string_view cookie_path);

  std::string BuildCookieHeaderLocked(std::string_view host,
                                      std::string_view target,
                                      bool is_https) const;

  void UpsertFromSetCookieLocked(std::string_view host, std::string_view target,
                                 std::string_view set_cookie_value);

  // Cookie jar (thread-safe via mutex_).
  mutable std::mutex mutex_;
  mutable std::vector<Cookie> cookies_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CONNECTION_CLIENT_HTTP_CLIENT_SESSION_H_
