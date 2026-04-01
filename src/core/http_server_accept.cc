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
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/socket.h>

#include <cerrno>
#endif

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/internal/connection/server/http_server_connection_impl.h"

using namespace bsrvcore;

namespace {

using tcp = boost::asio::ip::tcp;

bool TryEnableReusePort(tcp::acceptor& acceptor) {
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__)
#if defined(SO_REUSEPORT)
  const int option = 1;
  if (::setsockopt(acceptor.native_handle(), SOL_SOCKET, SO_REUSEPORT, &option,
                   sizeof(option)) != 0) {
    return false;
  }
  return true;
#else
  return false;
#endif
#else
  // Windows and other non-BSD targets currently do not implement SO_REUSEPORT.
  (void)acceptor;
  return false;
#endif
}

bool SetupEndpointAcceptor(tcp::acceptor& acceptor, const tcp::endpoint& ep,
                           bool try_reuse_port, bool require_reuse_port,
                           bool* reuse_port_enabled = nullptr) {
  boost::system::error_code ec;
  bool reuse_enabled = false;

  acceptor.open(ep.protocol(), ec);
  if (ec) {
    return false;
  }

  acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
  if (ec) {
    boost::system::error_code ignore_ec;
    acceptor.close(ignore_ec);
    return false;
  }

  if (try_reuse_port) {
    reuse_enabled = TryEnableReusePort(acceptor);
    if (require_reuse_port && !reuse_enabled) {
      boost::system::error_code ignore_ec;
      acceptor.close(ignore_ec);
      return false;
    }
  }

  acceptor.bind(ep, ec);
  if (ec) {
    boost::system::error_code ignore_ec;
    acceptor.close(ignore_ec);
    return false;
  }

  acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
  if (ec) {
    boost::system::error_code ignore_ec;
    acceptor.close(ignore_ec);
    return false;
  }

  if (reuse_port_enabled != nullptr) {
    *reuse_port_enabled = reuse_enabled;
  }

  return true;
}

void CloseAcceptor(tcp::acceptor& acceptor) {
  boost::system::error_code ec;
  acceptor.close(ec);
}

}  // namespace

void HttpServer::ClearExecutorSnapshotsLocked() {
  endpoint_io_execs_snapshot_.store(
      AllocateShared<std::vector<std::vector<boost::asio::any_io_executor>>>(),
      std::memory_order_release);
  global_io_execs_snapshot_.store(
      AllocateShared<std::vector<boost::asio::any_io_executor>>(),
      std::memory_order_release);
  io_exec_round_robin_.store(0, std::memory_order_relaxed);
}

void HttpServer::PublishExecutorSnapshotsLocked(
    std::vector<std::vector<boost::asio::any_io_executor>> endpoint_execs,
    std::vector<boost::asio::any_io_executor> global_execs) {
  endpoint_io_execs_snapshot_.store(
      AllocateShared<std::vector<std::vector<boost::asio::any_io_executor>>>(
          std::move(endpoint_execs)),
      std::memory_order_release);
  global_io_execs_snapshot_.store(
      AllocateShared<std::vector<boost::asio::any_io_executor>>(
          std::move(global_execs)),
      std::memory_order_release);
  io_exec_round_robin_.store(0, std::memory_order_relaxed);
}

void HttpServer::StopEndpointIoLocked() {
  for (auto& runtime : endpoint_runtimes_) {
    for (auto& acceptor : runtime->acceptors) {
      CloseAcceptor(acceptor);
    }
    runtime->io_work_guards.clear();
    for (auto& ioc : runtime->io_contexts) {
      ioc->stop();
    }
  }
}

void HttpServer::JoinEndpointIoThreadsLocked() {
  for (auto& runtime : endpoint_runtimes_) {
    for (auto& thread : runtime->io_threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }
    runtime->io_threads.clear();
  }
}

void HttpServer::ResetControlIoLocked() {
  if (control_io_work_guard_.has_value()) {
    control_io_work_guard_->reset();
    control_io_work_guard_.reset();
  }

  control_ioc_.stop();
  if (control_io_thread_.has_value() && control_io_thread_->joinable()) {
    control_io_thread_->join();
  }
  control_io_thread_.reset();
  control_ioc_.restart();
}

void HttpServer::RollbackStartLocked() {
  ClearExecutorSnapshotsLocked();
  StopEndpointIoLocked();
  JoinEndpointIoThreadsLocked();
  endpoint_runtimes_.clear();
  ResetControlIoLocked();
  reuse_port_supported_ = false;
  is_running_ = false;
}

bool HttpServer::BuildReusePortShardsLocked(
    const EndpointListenConfig& cfg, EndpointRuntime& runtime,
    std::size_t start_shard_index,
    std::vector<boost::asio::any_io_executor>& endpoint_execs,
    std::vector<boost::asio::any_io_executor>& global_execs) {
  for (std::size_t shard_index = start_shard_index;
       shard_index < cfg.io_threads; ++shard_index) {
    runtime.io_contexts.emplace_back(AllocateUnique<boost::asio::io_context>());
    auto& ioc = *runtime.io_contexts.back();
    runtime.io_work_guards.emplace_back(boost::asio::make_work_guard(ioc));
    runtime.acceptors.emplace_back(ioc);

    if (!SetupEndpointAcceptor(runtime.acceptors.back(), cfg.endpoint, true,
                               true, nullptr)) {
      return false;
    }

    auto exec = ioc.get_executor();
    endpoint_execs.push_back(exec);
    global_execs.push_back(exec);
  }

  return true;
}

bool HttpServer::BuildReusePortEndpointRuntimeLocked(
    const EndpointListenConfig& cfg, EndpointRuntime& runtime,
    std::vector<boost::asio::any_io_executor>& endpoint_execs,
    std::vector<boost::asio::any_io_executor>& global_execs) {
  runtime.io_contexts.reserve(cfg.io_threads);
  runtime.io_work_guards.reserve(cfg.io_threads);
  runtime.acceptors.reserve(cfg.io_threads);
  return BuildReusePortShardsLocked(cfg, runtime, 0, endpoint_execs,
                                    global_execs);
}

bool HttpServer::BuildFallbackEndpointRuntimeLocked(
    const EndpointListenConfig& cfg, EndpointRuntime& runtime,
    std::vector<boost::asio::any_io_executor>& endpoint_execs,
    std::vector<boost::asio::any_io_executor>& global_execs) {
  runtime.io_contexts.emplace_back(AllocateUnique<boost::asio::io_context>());
  auto& ioc = *runtime.io_contexts.back();
  runtime.io_work_guards.emplace_back(boost::asio::make_work_guard(ioc));
  runtime.acceptors.emplace_back(ioc);

  if (!SetupEndpointAcceptor(runtime.acceptors.back(), cfg.endpoint, false,
                             false, nullptr)) {
    return false;
  }

  auto exec = ioc.get_executor();
  endpoint_execs.push_back(exec);
  global_execs.push_back(exec);
  return true;
}

bool HttpServer::BuildFirstEndpointRuntimeLocked(
    const EndpointListenConfig& cfg, OwnedPtr<EndpointRuntime>& runtime,
    std::vector<boost::asio::any_io_executor>& endpoint_execs,
    std::vector<boost::asio::any_io_executor>& global_execs) {
  runtime->io_contexts.emplace_back(AllocateUnique<boost::asio::io_context>());
  auto& first_ioc = *runtime->io_contexts.back();
  runtime->io_work_guards.emplace_back(boost::asio::make_work_guard(first_ioc));
  runtime->acceptors.emplace_back(first_ioc);

  bool reuse_enabled = false;
  if (!SetupEndpointAcceptor(runtime->acceptors.back(), cfg.endpoint, true,
                             false, &reuse_enabled)) {
    return false;
  }

  reuse_port_supported_ = reuse_enabled;
  endpoint_execs.push_back(first_ioc.get_executor());
  global_execs.push_back(first_ioc.get_executor());

  if (!reuse_port_supported_) {
    return true;
  }

  runtime->io_contexts.reserve(cfg.io_threads);
  runtime->io_work_guards.reserve(cfg.io_threads);
  runtime->acceptors.reserve(cfg.io_threads);
  return BuildReusePortShardsLocked(cfg, *runtime, 1, endpoint_execs,
                                    global_execs);
}

bool HttpServer::BuildEndpointRuntimesLocked(
    std::vector<std::vector<boost::asio::any_io_executor>>& endpoint_execs,
    std::vector<boost::asio::any_io_executor>& global_execs) {
  endpoint_runtimes_.clear();
  reuse_port_supported_ = false;
  bool mode_decided = false;

  for (const auto& cfg : endpoint_configs_) {
    auto runtime = AllocateUnique<EndpointRuntime>(cfg.endpoint);
    runtime->run_threads = cfg.io_threads;
    endpoint_execs.emplace_back();

    if (!mode_decided) {
      // First endpoint decides runtime mode using REUSEPORT probe.
      if (!BuildFirstEndpointRuntimeLocked(cfg, runtime, endpoint_execs.back(),
                                           global_execs)) {
        return false;
      }
      mode_decided = true;
    } else if (reuse_port_supported_) {
      if (!BuildReusePortEndpointRuntimeLocked(cfg, *runtime,
                                               endpoint_execs.back(),
                                               global_execs)) {
        return false;
      }
    } else if (!BuildFallbackEndpointRuntimeLocked(cfg, *runtime,
                                                   endpoint_execs.back(),
                                                   global_execs)) {
      return false;
    }

    endpoint_runtimes_.emplace_back(std::move(runtime));
  }

  return true;
}

void HttpServer::StartEndpointRuntimesLocked() {
  for (std::size_t endpoint_index = 0;
       endpoint_index < endpoint_runtimes_.size(); ++endpoint_index) {
    auto& runtime = endpoint_runtimes_[endpoint_index];
    for (std::size_t shard_index = 0; shard_index < runtime->acceptors.size();
         ++shard_index) {
      DoAccept(endpoint_index, shard_index);
    }
  }

  for (auto& runtime : endpoint_runtimes_) {
    if (runtime->io_contexts.empty()) {
      continue;
    }

    if (reuse_port_supported_) {
      for (auto& ioc : runtime->io_contexts) {
        runtime->io_threads.emplace_back(
            [raw_ioc = ioc.get()] { raw_ioc->run(); });
      }
    } else {
      // Fallback mode: one acceptor + one io_context, but multiple run threads.
      auto* raw_ioc = runtime->io_contexts.front().get();
      for (std::size_t i = 0; i < runtime->run_threads; ++i) {
        runtime->io_threads.emplace_back([raw_ioc] { raw_ioc->run(); });
      }
    }
  }
}

bool HttpServer::Start() {
  std::unique_lock<std::mutex> lock(mtx_);

  if (is_running_) {
    return false;
  }

  if (kHasMaxConnection_) {
    available_connection_num_.store(
        static_cast<std::int64_t>(kRuntimeOptions_.max_connection),
        std::memory_order_relaxed);
  }

  is_running_ = true;

  control_ioc_.restart();
  control_io_work_guard_.emplace(boost::asio::make_work_guard(control_ioc_));
  control_io_thread_.emplace([this] { control_ioc_.run(); });

  std::vector<std::vector<boost::asio::any_io_executor>> endpoint_execs;
  endpoint_execs.reserve(endpoint_configs_.size());
  std::vector<boost::asio::any_io_executor> global_execs;

  if (!BuildEndpointRuntimesLocked(endpoint_execs, global_execs)) {
    RollbackStartLocked();
    return false;
  }

  PublishExecutorSnapshotsLocked(std::move(endpoint_execs),
                                 std::move(global_execs));
  StartEndpointRuntimesLocked();

  return true;
}

void HttpServer::Stop() {
  std::unique_lock<std::mutex> lock(mtx_);

  if (!is_running_) {
    return;
  }

  is_running_ = false;
  ClearExecutorSnapshotsLocked();
  StopEndpointIoLocked();

  // Key shutdown order: worker pool -> endpoint IO threads -> control IO.
  // This avoids posting work to components that have already been torn down.
  control_ioc_.stop();

  JoinThreadPool();
  JoinEndpointIoThreadsLocked();
  ResetControlIoLocked();

  ResetThreadPool();
  endpoint_runtimes_.clear();
  reuse_port_supported_ = false;
}

void HttpServer::StartAcceptedConnection(std::size_t endpoint_index,
                                         boost::asio::ip::tcp::socket socket) {
  boost::beast::tcp_stream stream(std::move(socket));

  if (ssl_ctx_.has_value()) {
    boost::beast::ssl_stream<boost::beast::tcp_stream> ssl_stream(
        std::move(stream), ssl_ctx_.value());
    auto ssl_exec = ssl_stream.get_executor();
    connection_internal::HttpServerConnectionImpl<boost::beast::ssl_stream<
        boost::beast::tcp_stream>>::Create(std::move(ssl_stream), ssl_exec,
                                           this, header_read_expiry_,
                                           keep_alive_timeout_,
                                           kHasMaxConnection_,
                                           &available_connection_num_,
                                           endpoint_index)
        ->Run();
    return;
  }

  auto stream_exec = stream.get_executor();
  connection_internal::HttpServerConnectionImpl<boost::beast::tcp_stream>::
      Create(std::move(stream), stream_exec, this, header_read_expiry_,
             keep_alive_timeout_, kHasMaxConnection_, &available_connection_num_,
             endpoint_index)
          ->Run();
}

void HttpServer::RearmAcceptIfRunning(std::size_t endpoint_index,
                                      std::size_t shard_index) {
  if (!is_running_.load(std::memory_order_acquire)) {
    return;
  }

  std::lock_guard<std::mutex> lock(mtx_);
  if (!is_running_ || endpoint_index >= endpoint_runtimes_.size()) {
    return;
  }

  auto* runtime = endpoint_runtimes_[endpoint_index].get();
  if (runtime == nullptr || shard_index >= runtime->acceptors.size()) {
    return;
  }

  DoAccept(endpoint_index, shard_index);
}

void HttpServer::HandleAcceptResult(std::size_t endpoint_index,
                                    std::size_t shard_index,
                                    boost::system::error_code ec,
                                    boost::asio::ip::tcp::socket socket) {
  const bool should_reject =
      !ec && kHasMaxConnection_ &&
      available_connection_num_.load(std::memory_order_relaxed) <= 0;

  if (!ec) {
    if (should_reject) {
      boost::system::error_code close_ec;
      socket.close(close_ec);
    } else {
      StartAcceptedConnection(endpoint_index, std::move(socket));
    }
  }

  RearmAcceptIfRunning(endpoint_index, shard_index);
}

void HttpServer::DoAccept(std::size_t endpoint_index, std::size_t shard_index) {
  if (endpoint_index >= endpoint_runtimes_.size()) {
    return;
  }

  auto* runtime = endpoint_runtimes_[endpoint_index].get();
  if (runtime == nullptr || shard_index >= runtime->acceptors.size()) {
    return;
  }

  auto& acceptor = runtime->acceptors[shard_index];
  acceptor.async_accept(
      [this, endpoint_index, shard_index](boost::system::error_code ec,
                                          boost::asio::ip::tcp::socket socket) {
        HandleAcceptResult(endpoint_index, shard_index, ec, std::move(socket));
      });
}
