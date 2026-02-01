/**
 * @file http_server_accept.cc
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
#include <shared_mutex>
#include <utility>

#include <bthpool/bthpool.hpp>

#include "bsrvcore/http_server.h"
#include "bsrvcore/internal/empty_logger.h"
#include "bsrvcore/internal/http_server_connection_impl.h"
#include "bsrvcore/internal/session_map.h"

using namespace bsrvcore;

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

  if (bth_pool_) {
    bth_pool_->join();
  }
  thread_pool_->join();
  for (auto& it : io_threads_) {
    it.join();
  }

  if (thread_cnt_) {
    thread_pool_ = std::make_unique<boost::asio::thread_pool>(thread_cnt_);
    bthpool::detail::BThreadPoolParam param;
    param.core_thread_num = thread_cnt_;
    param.max_thread_num = thread_cnt_;
    bth_pool_ = std::make_unique<bthpool::detail::BThreadPool>(param);
  } else {
    thread_pool_ = std::make_unique<boost::asio::thread_pool>();
    bth_pool_ = std::make_unique<bthpool::detail::BThreadPool>();
  }
  io_threads_.clear();
  acceptors_.clear();
  for (auto ep : eps) {
    acceptors_.emplace_back(ioc_, ep);
  }

  ioc_.restart();
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
