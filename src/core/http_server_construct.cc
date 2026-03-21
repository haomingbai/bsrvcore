/**
 * @file http_server_construct.cc
 * @brief HttpServer construction and destruction.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements HttpServer constructors and destructor.
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
#include <bthpool/bthpool.hpp>
#include <cstddef>
#include <cstring>
#include <memory>

#include "bsrvcore/allocator.h"
#include "bsrvcore/context.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/internal/empty_logger.h"
#include "bsrvcore/internal/http_route_table.h"
#include "bsrvcore/internal/http_server_connection_impl.h"
#include "bsrvcore/internal/session_map.h"

using namespace bsrvcore;

namespace {

bthpool::detail::BThreadPoolParam ToBThreadPoolParam(
    const HttpServerExecutorOptions& options) {
  bthpool::detail::BThreadPoolParam param;
  param.core_thread_num = options.core_thread_num;
  param.max_thread_num = options.max_thread_num;
  param.fast_queue_capacity = options.fast_queue_capacity;
  param.thread_clean_interval = options.thread_clean_interval;
  param.task_scan_interval = options.task_scan_interval;
  param.suspend_time = options.suspend_time;
  param.memory_resource = bsrvcore::GetDefaultMemoryResource();
  return param;
}

HttpServerExecutorOptions MakeExecutorOptionsFromThreadNum(
    std::size_t thread_num) {
  HttpServerExecutorOptions options;
  if (thread_num != 0) {
    options.core_thread_num = thread_num;
    options.max_thread_num = thread_num;
  }
  return options;
}

}  // namespace

HttpServer::HttpServer(std::size_t thread_num)
    : HttpServer(MakeExecutorOptionsFromThreadNum(thread_num)) {}

HttpServer::HttpServer(HttpServerExecutorOptions executor_options)
    : context_(bsrvcore::AllocateShared<Context>()),
      logger_(bsrvcore::AllocateShared<internal::EmptyLogger>()),
      thread_pool_(bsrvcore::AllocateUnique<bthpool::detail::BThreadPool>(
          ToBThreadPoolParam(executor_options))),
      route_table_(bsrvcore::AllocateUnique<HttpRouteTable>()),
      sessions_(
          bsrvcore::AllocateUnique<SessionMap>(ioc_.get_executor(), this)),
      header_read_expiry_(3000),
      keep_alive_timeout_(4000),
      executor_options_(std::move(executor_options)),
      is_running_(false) {}

HttpServer::HttpServer() : HttpServer(HttpServerExecutorOptions{}) {}

HttpServer::~HttpServer() { Stop(); }
