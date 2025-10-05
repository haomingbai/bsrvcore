/**
 * @file http_server_construct.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

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
#include <memory>

#include "bsrvcore/context.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/internal/empty_logger.h"
#include "bsrvcore/internal/http_route_table.h"
#include "bsrvcore/internal/http_server_connection_impl.h"
#include "bsrvcore/internal/session_map.h"

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

HttpServer::~HttpServer() { Stop(); }
