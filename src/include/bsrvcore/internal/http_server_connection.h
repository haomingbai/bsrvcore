/**
 * @file http_server_connection.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-25
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_H_
#define BSRVCORE_INTERNAL_HTTP_SERVER_CONNECTION_H_

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/fields_fwd.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/parser_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "bsrvcore/http_server_task.h"
#include "bsrvcore/internal/http_route_table.h"
#include "bsrvcore/logger.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

class HttpServer;

class Context;

class HttpServerConnection : NonCopyableNonMovable<HttpServerConnection> {
 public:
  boost::asio::strand<boost::asio::io_context> GetExecutor();

  void Post(std::function<void()>);

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

  void SetTimer(std::size_t timeout, std::function<void()>);

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

  std::shared_ptr<Context> GetContext() noexcept;

  std::shared_ptr<Context> GetSession(const std::string& sessionid) noexcept;

  std::shared_ptr<Context> GetSession(std::string&& sessionid) noexcept;

  bool SetSessionTimeout(const std::string&, std::size_t);

  bool SetSessionTimeout(std::string&&, std::size_t);

  void Log(LogLevel level, std::string_view message);

  virtual bool IsStreamAvailable() noexcept;

  void Run();

  void DoWriteResponse();

  void DoFlushResponseHeader(
      boost::beast::http::response_header<boost::beast::http::fields> header);

  void DoFlushResponseBody(std::string body);

  void DoClose();

  void DoCycle();

  virtual ~HttpServerConnection();

 protected:
  boost::beast::flat_buffer& GetBuffer();

 private:
  boost::asio::strand<boost::asio::io_context> strand_;
  boost::asio::steady_timer timer_;
  boost::shared_ptr<HttpServer> srv_;
  boost::beast::flat_buffer buf_;
  HttpRouteResult route_result_;
  std::unique_ptr<boost::beast::http::request_parser<
      boost::beast::http::string_body, boost::beast::http::fields>>
      parser_;
  std::size_t header_read_expiry_;

  virtual void DoReadHeader() = 0;

  void DoRoute();

  virtual void DoReadBody() = 0;

  void DoForwardRequest();
};

}  // namespace bsrvcore

#endif
