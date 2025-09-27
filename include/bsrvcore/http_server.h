/**
 * @file http_server.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-26
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#ifndef BSRVCORE_HTTP_SERVER_H_
#define BSRVCORE_HTTP_SERVER_H_

#include <algorithm>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http/verb.hpp>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>

#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server_task.h"
#include "bsrvcore/internal/http_route_table.h"
#include "bsrvcore/logger.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

class HttpServer : std::enable_shared_from_this<HttpServer>,
                   NonCopyableNonMovable<HttpServer> {
 public:
  void SetTimer(std::size_t timeout, std::function<void()> fn);

  template <typename Fn, typename... Args>
  auto SetTimer(std::size_t timeout, Fn fn, Args &&...args)
      -> std::future<std::invoke_result_t<Fn, Args...>> {
    using RT = typename std::invoke_result_t<Fn, Args...>;

    auto binded_fn = std::bind(fn, std::forward<Args>(args)...);
    auto task = std::make_shared<std::packaged_task<RT()>>(binded_fn);
    auto future = task->get_future();
    std::function<void()> to_post = [task]() { (*task)(); };

    SetTimer(timeout, to_post);

    return future;
  }

  void Post(std::function<void()> fn);

  template <typename Fn, typename... Args>
  auto Post(Fn fn, Args &&...args)
      -> std::future<std::invoke_result_t<Fn, Args...>> {
    using RT = typename std::invoke_result_t<Fn, Args...>;

    auto binded_fn = std::bind(fn, std::forward<Args>(args)...);
    auto task = std::make_shared<std::packaged_task<RT()>>(binded_fn);
    auto future = task->get_future();
    std::function<void()> to_post = [task]() { (*task)(); };

    Post(to_post);

    return future;
  }

  std::shared_ptr<HttpServer> AddRouteEntry(
      HttpRequestMethod method, std::string_view url,
      std::unique_ptr<HttpRequestHandler> handler);

  template <typename Func>
    requires requires(std::shared_ptr<HttpServerTask> task, Func fn) {
      { fn(task) };
    }
  std::shared_ptr<HttpServer> AddRouteEntry(HttpRequestMethod method,
                                            std::string_view str, Func &&func) {
    auto handler = std::make_unique<FunctionRouteHandler<Func>>(func);

    return AddRouteEntry(method, str, std::move(handler));
  }

  std::shared_ptr<HttpServer> AddExclusiveRouteEntry(
      HttpRequestMethod method, std::string_view url,
      std::unique_ptr<HttpRequestHandler> handler);

  template <typename Func>
    requires requires(std::shared_ptr<HttpServerTask> task, Func fn) {
      { fn(task) };
    }
  std::shared_ptr<HttpServer> AddExclusiveRouteEntry(HttpRequestMethod method,
                                                     std::string_view str,
                                                     Func &&func) {
    auto handler = std::make_unique<FunctionRouteHandler<Func>>(func);

    return AddExclusiveRouteEntry(method, str, std::move(handler));
  }

  std::shared_ptr<HttpServer> AddListen(boost::asio::ip::tcp::endpoint ep);

  std::shared_ptr<HttpServer> SetReadExpiry(HttpRequestMethod method,
                                            std::string_view url,
                                            std::size_t expiry);

  std::shared_ptr<HttpServer> SetHeaderReadExpiry(std::size_t expiry);

  std::shared_ptr<HttpServer> SetWriteExpiry(HttpRequestMethod method,
                                             std::string_view url,
                                             std::size_t expiry);

  std::shared_ptr<HttpServer> SetMaxBodySize(HttpRequestMethod method,
                                             std::string_view url,
                                             std::size_t size);

  std::shared_ptr<HttpServer> SetDefaultReadExpiry(std::size_t expiry);

  std::shared_ptr<HttpServer> SetDefaultWriteExpiry(std::size_t expiry);

  std::shared_ptr<HttpServer> SetDefaultMaxBodySize(std::size_t size);

  std::shared_ptr<HttpServer> SetKeepAliveTimeout(std::size_t timeout);

  void Log(LogLevel level, std::string_view message);

  HttpRouteResult Route(std::string_view target);

  std::shared_ptr<Context> GetSession(const std::string &sessionid);

  std::shared_ptr<Context> GetSession(std::string &sessionid);

  void SetDefaultSessionTimeout(std::size_t timeout);

  bool SetSessionTimeout(const std::string &sessionid);

  bool SetSessionTimeout(std::string &sessionid);

  std::shared_ptr<Context> GetContext();

  std::size_t GetKeepAliveTimeout();

  bool IsRunning();

  void Stop();

  bool Start(std::size_t);

  static HttpRequestMethod BeastHttpVerbToHttpRequestMethod(
      boost::beast::http::verb verb);

  static boost::beast::http::verb HttpRequestMethodToBeastHttpVerb(
      HttpRequestMethod);
};

}  // namespace bsrvcore

#endif
