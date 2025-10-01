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
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "bsrvcore/http_route_result.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "bsrvcore/logger.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

class HttpServer;

class Context;

class HttpServerConnection
    : public NonCopyableNonMovable<HttpServerConnection>,
      protected std::enable_shared_from_this<HttpServerConnection> {
 public:
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

  std::shared_ptr<Context> GetSession(const std::string& sessionid);

  std::shared_ptr<Context> GetSession(std::string&& sessionid);

  bool SetSessionTimeout(const std::string&, std::size_t);

  bool SetSessionTimeout(std::string&&, std::size_t);

  void Log(LogLevel level, std::string message);

  bool IsServerRunning() const noexcept;

  void Run();

  virtual void DoWriteResponse(HttpResponse resp, bool keep_alive) = 0;

  virtual void DoFlushResponseHeader(
      boost::beast::http::response_header<boost::beast::http::fields>
          header) = 0;

  virtual void DoFlushResponseBody(std::string body) = 0;

  virtual void DoClose() = 0;

  void DoCycle();

  HttpServerConnection(
      boost::asio::strand<boost::asio::io_context::executor_type> strand,
      std::shared_ptr<HttpServer> srv, std::size_t header_read_expiry,
      std::size_t keep_alive_timeout);

  virtual ~HttpServerConnection() = default;

 protected:
  boost::beast::flat_buffer& GetBuffer();

  boost::asio::strand<boost::asio::io_context::executor_type> GetExecutor();

  std::unique_ptr<
      boost::beast::http::request_parser<boost::beast::http::string_body>>&
  GetParser() noexcept;

  void MakeHttpServerTask();

  void DoRoute();

  virtual void DoReadHeader() = 0;

  virtual void DoReadBody() = 0;

  void DoPreService(std::shared_ptr<HttpServerTask> task, std::size_t curr_idx);

  void DoPostService(std::shared_ptr<HttpServerTask> task,
                     std::size_t curr_idx);

  void DoForwardRequest(std::shared_ptr<HttpServerTask> task);

  virtual bool IsStreamAvailable() const noexcept = 0;

  boost::asio::strand<boost::asio::io_context::executor_type>& GetStrand();

 private:
  boost::asio::strand<boost::asio::io_context::executor_type> strand_;
  boost::asio::steady_timer timer_;
  boost::beast::flat_buffer buf_;
  HttpRouteResult route_result_;
  std::shared_ptr<HttpServer> srv_;
  std::unique_ptr<
      boost::beast::http::request_parser<boost::beast::http::string_body>>
      parser_;
  std::size_t header_read_expiry_;
  std::size_t keep_alive_timeout_;
};

}  // namespace bsrvcore

#endif
