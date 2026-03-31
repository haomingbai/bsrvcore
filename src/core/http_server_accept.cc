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

#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/internal/connection/server/http_server_connection_impl.h"

using namespace bsrvcore;

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

  JoinThreadPool();
  for (auto& it : io_threads_) {
    it.join();
  }

  ResetThreadPool();
  io_threads_.clear();
  acceptors_.clear();
  for (auto ep : eps) {
    acceptors_.emplace_back(ioc_, ep);
  }

  ioc_.restart();
}

void HttpServer::DoAccept(boost::asio::ip::tcp::acceptor& acc) {
  const auto close_socket = [](boost::asio::ip::tcp::socket& socket) {
    boost::system::error_code close_ec;
    socket.close(close_ec);
  };

  acc.async_accept(
      boost::asio::make_strand(ioc_),
      [this, &acc, close_socket](boost::system::error_code ec,
                                 boost::asio::ip::tcp::socket skt) {
        const auto should_reject_socket = [&]() {
          return kHasMaxConnection_ &&
                 available_connection_num_.load(std::memory_order_relaxed) <= 0;
        };

        const auto start_ssl_connection = [&](boost::beast::tcp_stream stream) {
          boost::beast::ssl_stream<boost::beast::tcp_stream> ssl_stream(
              std::move(stream), ssl_ctx_.value());
          connection_internal::HttpServerConnectionImpl<
              boost::beast::ssl_stream<boost::beast::tcp_stream>>::
              Create(std::move(ssl_stream), this, header_read_expiry_,
                     keep_alive_timeout_, kHasMaxConnection_,
                     &available_connection_num_)
                  ->Run();
        };

        const auto start_plain_connection =
            [&](boost::beast::tcp_stream stream) {
              connection_internal::HttpServerConnectionImpl<
                  boost::beast::tcp_stream>::Create(std::move(stream), this,
                                                    header_read_expiry_,
                                                    keep_alive_timeout_,
                                                    kHasMaxConnection_,
                                                    &available_connection_num_)
                  ->Run();
            };

        // Keep the accept callback as a coordinator: reject overflow sockets,
        // choose the transport-specific connection type, then re-arm the loop.
        if (!ec) {
          if (should_reject_socket()) {
            close_socket(skt);
          } else {
            boost::beast::tcp_stream stream(std::move(skt));
            if (ssl_ctx_.has_value()) {
              start_ssl_connection(std::move(stream));
            } else {
              start_plain_connection(std::move(stream));
            }
          }
        }

        if (is_running_) {
          DoAccept(acc);
        }
      });
}
