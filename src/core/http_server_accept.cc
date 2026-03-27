/**
 * @file http_server_accept.cc
 * @brief HttpServer accept loop implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements start/stop and asynchronous acceptor loop.
 */

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/detail/chrono.hpp>
#include <boost/asio/executor_work_guard.hpp>
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
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <shared_mutex>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/internal/core/empty_logger.h"
#include "bsrvcore/internal/connection/server/http_server_connection_impl.h"
#include "bsrvcore/internal/session/session_map.h"

using namespace bsrvcore;

namespace {

bthpool::BThreadPoolParam ToThreadPoolParam(
    const HttpServerRuntimeOptions& options) {
  bthpool::BThreadPoolParam param;
  param.core_thread_num = options.core_thread_num;
  param.max_thread_num = options.max_thread_num;
  param.fast_queue_capacity =
      options.fast_queue_capacity == 0 ? 1 : options.fast_queue_capacity;
  param.thread_clean_interval = options.thread_clean_interval;
  param.task_scan_interval = options.task_scan_interval;
  param.suspend_time = options.suspend_time;
  return param;
}

}  // namespace

bool HttpServer::Start(std::size_t thread_cnt) {
  if (thread_cnt == 0) {
    return false;
  }

  std::unique_lock<std::shared_mutex> lock(mtx_);

  if (is_running_) {
    return false;
  }

  if (kHasMaxConnection_) {
    available_connection_num_.store(
        static_cast<std::int64_t>(kRuntimeOptions_.max_connection),
        std::memory_order_relaxed);
  }

  is_running_ = true;
  ioc_.restart();
  io_work_guard_.emplace(boost::asio::make_work_guard(ioc_));

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

  if (io_work_guard_.has_value()) {
    io_work_guard_->reset();
    io_work_guard_.reset();
  }

  thread_pool_->join();
  for (auto& it : io_threads_) {
    it.join();
  }

  thread_pool_ =
      bsrvcore::AllocateUnique<bthpool::BThreadPool<Allocator<std::byte>>>(
          ToThreadPoolParam(kRuntimeOptions_), Allocator<std::byte>{});
  io_threads_.clear();
  acceptors_.clear();
  for (auto ep : eps) {
    acceptors_.emplace_back(ioc_, ep);
  }

  ioc_.restart();
}

void HttpServer::DoAccept(boost::asio::ip::tcp::acceptor& acc) {
  static bsrvcore::Allocator<std::byte> accept_alloc{};

  acc.async_accept(
      boost::asio::make_strand(ioc_),
      boost::asio::bind_allocator(
          accept_alloc, [this, &acc](boost::system::error_code ec,
                                     boost::asio::ip::tcp::socket skt) {
            if (!ec) {
              if (kHasMaxConnection_ &&
                  available_connection_num_.load(std::memory_order_relaxed) <=
                      0) {
                boost::system::error_code close_ec;
                skt.close(close_ec);
              } else {
                boost::beast::tcp_stream stream(std::move(skt));
                if (ssl_ctx_.has_value()) {
                  boost::beast::ssl_stream<boost::beast::tcp_stream> sstream(
                      std::move(stream), ssl_ctx_.value());
                  connection_internal::HttpServerConnectionImpl<
                      boost::beast::ssl_stream<boost::beast::tcp_stream>>::
                      Create(std::move(sstream),
                             boost::asio::strand<boost::asio::any_io_executor>(
                                 sstream.get_executor()),
                             this, header_read_expiry_, keep_alive_timeout_,
                             kHasMaxConnection_, &available_connection_num_)
                          ->Run();
                } else {
                  connection_internal::HttpServerConnectionImpl<
                      boost::beast::tcp_stream>::Create(
                      std::move(stream),
                      boost::asio::strand<boost::asio::any_io_executor>(
                          stream.get_executor()),
                      this, header_read_expiry_, keep_alive_timeout_,
                      kHasMaxConnection_, &available_connection_num_)
                      ->Run();
                }
              }
            }

            if (is_running_) {
              DoAccept(acc);
            }
          }));
}
