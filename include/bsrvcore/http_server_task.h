/**
 * @file http_server_task.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-24
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#include <vector>
#ifndef BSRVCORE_HTTP_SERVER_TASK_H_
#define BSRVCORE_HTTP_SERVER_TASK_H_

#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields_fwd.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/logger.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

class HttpServerConnection;

class Context;

using HttpRequest = boost::beast::http::request<boost::beast::http::string_body,
                                                boost::beast::http::fields>;

using HttpResponse =
    boost::beast::http::response<boost::beast::http::string_body,
                                 boost::beast::http::fields>;

using HttpResponseHeader =
    boost::beast::http::response_header<boost::beast::http::fields>;

using HttpRequestHeader =
    boost::beast::http::request_header<boost::beast::http::fields>;

class HttpServerTask : NonCopyableNonMovable<HttpServerTask> {
 public:
  HttpRequest& GetRequest() noexcept;

  HttpResponse& GetResponse() noexcept;

  std::shared_ptr<Context> GetSession() noexcept;

  bool SetSessionTimeout(std::size_t);

  void SetBody(std::string body);

  void AppendBody(const std::string_view body);

  void SetField(const std::string_view key, const std::string_view value);

  void SetField(boost::beast::http::field key, const std::string_view value);

  void SetAutowrite(bool value) noexcept;

  void SetKeepAlive(bool value) noexcept;

  std::shared_ptr<Context> GetContext() noexcept;

  void Log(LogLevel level, const std::string_view message);

  void WriteBody(std::string body);

  void WriteHeader(HttpResponseHeader header);

  void Post(std::function<void()> fn);

  template <typename Fn, typename... Args>
  auto Post(Fn fn, Args&&... args)
      -> std::future<std::invoke_result_t<Fn, Args...>> {
    using RT = typename std::invoke_result_t<Fn, Args...>;

    auto binded_fn = std::bind(fn, std::forward<Args>(args)...);
    auto task = std::make_shared<std::packaged_task<RT()>>(binded_fn);
    auto future = task->get_future();
    std::function<void()> to_post = [task]() { (*task)(); };

    Post(to_post);

    return future;
  }

  void SetTimer(std::size_t timeout, std::function<void()> fn);

  template <typename Fn, typename... Args>
  auto SetTimer(std::size_t timeout, Fn fn, Args&&... args)
      -> std::future<std::invoke_result_t<Fn, Args...>> {
    using RT = typename std::invoke_result_t<Fn, Args...>;

    auto binded_fn = std::bind(fn, std::forward<Args>(args)...);
    auto task = std::make_shared<std::packaged_task<RT()>>(binded_fn);
    auto future = task->get_future();
    std::function<void()> to_post = [task]() { (*task)(); };

    SetTimer(timeout, to_post);

    return future;
  }

  bool IsAvailable() noexcept;

  void DoClose();

 private:
  HttpRequest req_;
  HttpResponse resp_;
  std::vector<std::string> parameters;
  std::string current_location_;
  std::shared_ptr<HttpServerConnection> conn_;
  bool keep_alive_;
  bool autowrite_;
};

}  // namespace bsrvcore

#endif
