/**
 * @file http_server.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/http_server.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/detail/chrono.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/context.h"
#include "bsrvcore/http_request_aspect_handler.h"
#include "bsrvcore/http_request_handler.h"
#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_route_result.h"
#include "bsrvcore/internal/empty_logger.h"
#include "bsrvcore/internal/http_route_table.h"
#include "bsrvcore/internal/http_server_connection_impl.h"
#include "bsrvcore/internal/session_map.h"
#include "bsrvcore/logger.h"

using namespace bsrvcore;

HttpServer::HttpServer(std::size_t thread_num)
    : context_(std::make_shared<Context>()),
      logger_(std::make_shared<internal::EmptyLogger>()),
      thread_pool_(std::make_unique<boost::asio::thread_pool>(thread_num)),
      route_table_(std::make_unique<HttpRouteTable>()),
      sessions_(
          std::make_unique<SessionMap>(thread_pool_->get_executor(), this)),
      header_read_expiry_(3000),
      keep_alive_timeout_(4000),
      thread_cnt_(thread_num),
      is_running_(false) {}

HttpServer::HttpServer()
    : context_(std::make_shared<Context>()),
      logger_(std::make_shared<internal::EmptyLogger>()),
      thread_pool_(std::make_unique<boost::asio::thread_pool>()),
      route_table_(std::make_unique<HttpRouteTable>()),
      sessions_(
          std::make_unique<SessionMap>(thread_pool_->get_executor(), this)),
      header_read_expiry_(3000),
      keep_alive_timeout_(4000),
      thread_cnt_(0),
      is_running_(false) {}

void HttpServer::SetTimer(std::size_t timeout, std::function<void()> fn) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (!is_running_) {
    return;
  }

  auto timer =
      std::make_shared<boost::asio::steady_timer>(thread_pool_->get_executor());
  timer->expires_after(boost::asio::chrono::milliseconds(timeout));
  timer->async_wait([fn, timer](boost::system::error_code ec) {
    if (!ec) {
      fn();
    }
  });
}

void HttpServer::Post(std::function<void()> fn) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (!is_running_) {
    return;
  }

  boost::asio::post(thread_pool_->get_executor(), fn);
}

HttpServer* HttpServer::AddRouteEntry(
    HttpRequestMethod method, const std::string_view url,
    std::unique_ptr<HttpRequestHandler> handler) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddRouteEntry(method, url, std::move(handler));
  return this;
}

HttpServer* HttpServer::AddExclusiveRouteEntry(
    HttpRequestMethod method, const std::string_view url,
    std::unique_ptr<HttpRequestHandler> handler) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddExclusiveRouteEntry(method, url, std::move(handler));
  return this;
}

HttpServer* HttpServer::AddAspect(
    HttpRequestMethod method, const std::string_view url,
    std::unique_ptr<HttpRequestAspectHandler> aspect) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddAspect(method, url, std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddGlobalAspect(
    HttpRequestMethod method,
    std::unique_ptr<HttpRequestAspectHandler> aspect) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddGlobalAspect(method, std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddGlobalAspect(
    std::unique_ptr<HttpRequestAspectHandler> aspect) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->AddGlobalAspect(std::move(aspect));
  return this;
}

HttpServer* HttpServer::AddListen(boost::asio::ip::tcp::endpoint ep) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  acceptors_.emplace_back(ioc_, ep);
  return this;
}

HttpServer* HttpServer::SetReadExpiry(HttpRequestMethod method,
                                      std::string_view url,
                                      std::size_t expiry) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetReadExpiry(method, url, expiry);
  return this;
}

HttpServer* HttpServer::SetHeaderReadExpiry(std::size_t expiry) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  header_read_expiry_ = expiry;
  return this;
}

HttpServer* HttpServer::SetWriteExpiry(HttpRequestMethod method,
                                       std::string_view url,
                                       std::size_t expiry) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetWriteExpiry(method, url, expiry);
  return this;
}

HttpServer* HttpServer::SetMaxBodySize(HttpRequestMethod method,
                                       std::string_view url, std::size_t size) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetMaxBodySize(method, url, size);
  return this;
}

HttpServer* HttpServer::SetDefaultReadExpiry(std::size_t expiry) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultReadExpiry(expiry);
  return this;
}

HttpServer* HttpServer::SetDefaultWriteExpiry(std::size_t expiry) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultWriteExpiry(expiry);
  return this;
}

HttpServer* HttpServer::SetDefaultMaxBodySize(std::size_t size) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultMaxBodySize(size);
  return this;
}

HttpServer* HttpServer::SetKeepAliveTimeout(std::size_t timeout) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  keep_alive_timeout_ = timeout;
  return this;
}

HttpServer* HttpServer::SetDefaultHandler(
    std::unique_ptr<HttpRequestHandler> handler) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  route_table_->SetDefaultHandler(std::move(handler));
  return this;
}

HttpServer* HttpServer::SetSslContext(boost::asio::ssl::context ctx) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  ssl_ctx_ = std::move(ctx);
  return this;
}

HttpServer* HttpServer::UnsetSslContext() {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  ssl_ctx_.reset();
  return this;
}

HttpServer* HttpServer::SetLogger(std::shared_ptr<Logger> logger) {
  std::shared_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return this;
  }

  logger_ = logger;
  return this;
}

void HttpServer::Log(LogLevel level, std::string message) {
  logger_->Log(level, std::move(message));
}

HttpRouteResult HttpServer::Route(HttpRequestMethod method,
                                  std::string_view target) {
  return route_table_->Route(method, target);
}

std::shared_ptr<Context> HttpServer::GetSession(const std::string& sessionid) {
  return sessions_->GetSession(sessionid);
}

std::shared_ptr<Context> HttpServer::GetSession(std::string&& sessionid) {
  return sessions_->GetSession(std::move(sessionid));
}

void HttpServer::SetDefaultSessionTimeout(std::size_t timeout) {
  sessions_->SetDefaultSessionTimeout(timeout);
}

bool HttpServer::SetSessionTimeout(const std::string& sessionid,
                                   std::size_t timeout) {
  sessions_->SetSessionTimeout(sessionid, timeout);
  return true;
}

bool HttpServer::SetSessionTimeout(std::string&& sessionid,
                                   std::size_t timeout) {
  sessions_->SetSessionTimeout(sessionid, timeout);
  return true;
}

std::shared_ptr<Context> HttpServer::GetContext() { return context_; }

std::size_t HttpServer::GetKeepAliveTimeout() { return keep_alive_timeout_; }

bool HttpServer::IsRunning() { return is_running_; }

bool HttpServer::Start(std::size_t thread_cnt) {
  if (thread_cnt == 0) {
    return false;
  }

  std::unique_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return false;
  }

  is_running_ = true;

  for (auto& acc : acceptors_) {
    DoAccept(acc);
  }

  io_threads_.reserve(thread_cnt);
  for (size_t i = 0; i < thread_cnt; i++) {
    auto th = std::thread([this] { ioc_.run(); });
    io_threads_.emplace_back(std::move(th));
  }

  return true;
}

void HttpServer::Stop() {
  std::vector<boost::asio::ip::tcp::endpoint> eps;
  eps.reserve(acceptors_.size());

  std::unique_lock<std::shared_mutex> lock(mtx_);

  if (!is_running_) {
    return;
  }

  is_running_ = false;
  for (auto& acc : acceptors_) {
    eps.push_back(acc.local_endpoint());
    boost::system::error_code ec1;
    boost::system::error_code ec2;
    ec2 = acc.close(ec1);
  }

  thread_pool_->join();
  for (auto& it : io_threads_) {
    it.join();
  }

  if (thread_cnt_) {
    thread_pool_ = std::make_unique<boost::asio::thread_pool>(thread_cnt_);
  } else {
    thread_pool_ = std::make_unique<boost::asio::thread_pool>();
  }
  io_threads_.clear();
  acceptors_.clear();
  for (auto ep : eps) {
    acceptors_.emplace_back(ioc_, ep);
  }

  ioc_.restart();
}

HttpRequestMethod HttpServer::BeastHttpVerbToHttpRequestMethod(
    boost::beast::http::verb verb) {
  switch (verb) {
    case boost::beast::http::verb::get:
      return HttpRequestMethod::kGet;
    case boost::beast::http::verb::post:
      return HttpRequestMethod::kPost;
    case boost::beast::http::verb::put:
      return HttpRequestMethod::kPut;
    case boost::beast::http::verb::delete_:
      return HttpRequestMethod::kDelete;
    case boost::beast::http::verb::patch:
      return HttpRequestMethod::kPatch;
    case boost::beast::http::verb::head:
      return HttpRequestMethod::kHead;
    default:
      return HttpRequestMethod::kGet;
  }
}

boost::beast::http::verb HttpServer::HttpRequestMethodToBeastHttpVerb(
    HttpRequestMethod method) {
  switch (method) {
    case HttpRequestMethod::kGet:
      return boost::beast::http::verb::get;
    case HttpRequestMethod::kPost:
      return boost::beast::http::verb::post;
    case HttpRequestMethod::kPut:
      return boost::beast::http::verb::put;
    case HttpRequestMethod::kDelete:
      return boost::beast::http::verb::delete_;
    case HttpRequestMethod::kPatch:
      return boost::beast::http::verb::patch;
    case HttpRequestMethod::kHead:
      return boost::beast::http::verb::head;
  }
  return boost::beast::http::verb::get;
}

void HttpServer::DoAccept(boost::asio::ip::tcp::acceptor& acc) {
  acc.async_accept(
      boost::asio::make_strand(ioc_),
      [this, &acc](boost::system::error_code ec,
                   boost::asio::ip::tcp::socket skt) {
        if (!ec) {
          boost::beast::tcp_stream stream(std::move(skt));
          if (ssl_ctx_.has_value()) {
            boost::beast::ssl_stream<boost::beast::tcp_stream> sstream(
                std::move(stream), ssl_ctx_.value());
            std::make_shared<connection_internal::HttpServerConnectionImpl<
                boost::beast::ssl_stream<boost::beast::tcp_stream>>>(
                std::move(sstream),
                boost::asio::strand<boost::asio::any_io_executor>(
                    sstream.get_executor()),
                this, header_read_expiry_, keep_alive_timeout_)
                ->Run();
          } else {
            std::make_shared<connection_internal::HttpServerConnectionImpl<
                boost::beast::tcp_stream>>(
                std::move(stream),
                boost::asio::strand<boost::asio::any_io_executor>(
                    stream.get_executor()),
                this, header_read_expiry_, keep_alive_timeout_)
                ->Run();
          }
        }

        if (is_running_) {
          DoAccept(acc);
        }
      });
}

HttpServer::~HttpServer() { Stop(); }
