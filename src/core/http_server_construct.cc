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

#include <bthpool/bthpool.hpp>
#include <cstddef>
#include <memory>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/internal/core/empty_logger.h"
#include "bsrvcore/internal/route/http_route_table.h"
#include "bsrvcore/internal/session/session_map.h"
#include "bsrvcore/session/context.h"

using namespace bsrvcore;

namespace {

bthpool::detail::BThreadPoolParam ToThreadPoolParam(
    const HttpServerRuntimeOptions& options) {
  bthpool::detail::BThreadPoolParam param;
  param.core_thread_num = options.core_thread_num;
  param.max_thread_num = options.max_thread_num;
  param.fast_queue_capacity =
      options.fast_queue_capacity == 0 ? 1 : options.fast_queue_capacity;
  param.thread_clean_interval = options.thread_clean_interval;
  param.task_scan_interval = options.task_scan_interval;
  param.suspend_time = options.suspend_time;
  return param;
}

HttpServerRuntimeOptions MakeRuntimeOptionsFromThreadNum(
    std::size_t thread_num) {
  HttpServerRuntimeOptions options;
  if (thread_num != 0) {
    options.core_thread_num = thread_num;
    options.max_thread_num = thread_num;
  }
  return options;
}

}  // namespace

HttpServer::HttpServer(std::size_t thread_num)
    : HttpServer(MakeRuntimeOptionsFromThreadNum(thread_num)) {}

HttpServer::HttpServer(HttpServerRuntimeOptions runtime_options)
    : context_(bsrvcore::AllocateShared<Context>()),
      logger_(bsrvcore::AllocateShared<internal::EmptyLogger>()),
      thread_pool_(
          bsrvcore::AllocateUnique<bthpool::BThreadPool<Allocator<std::byte>>>(
              ToThreadPoolParam(runtime_options))),
      route_table_(bsrvcore::AllocateUnique<HttpRouteTable>()),
      sessions_(
          bsrvcore::AllocateUnique<SessionMap>(ioc_.get_executor(), this)),
      header_read_expiry_(3000),
      keep_alive_timeout_(4000),
      kRuntimeOptions_(std::move(runtime_options)),
      kHasMaxConnection_(kRuntimeOptions_.has_max_connection),
      available_connection_num_(
          kHasMaxConnection_
              ? static_cast<std::int64_t>(kRuntimeOptions_.max_connection)
              : static_cast<std::int64_t>(0)),
      is_running_(false) {}

HttpServer::HttpServer() : HttpServer(HttpServerRuntimeOptions{}) {}

HttpServer::~HttpServer() { Stop(); }
