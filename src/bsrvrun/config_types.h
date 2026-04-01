/**
 * @file config_types.h
 * @brief Configuration model types for bsrvrun.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_CONFIG_TYPES_H_
#define BSRVCORE_BSRVRUN_CONFIG_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "bsrvcore/route/http_request_method.h"

namespace bsrvcore::runtime {

struct FactoryConfig {
  std::string library;
  std::unordered_map<std::string, std::string> params;
};

struct ListenerConfig {
  std::string address;
  std::uint16_t port;
  std::size_t io_threads{1};
};

struct RouteConfig {
  bsrvcore::HttpRequestMethod method;
  std::string path;
  bool ignore_default_route;
  bool cpu{false};
  FactoryConfig handler;
  std::vector<FactoryConfig> aspects;
};

struct GlobalConfig {
  std::optional<FactoryConfig> default_handler;
  std::vector<FactoryConfig> aspects;
};

struct ExecutorConfig {
  bool configured{false};
  std::size_t core_thread_num{std::thread::hardware_concurrency()};
  std::size_t max_thread_num{std::numeric_limits<int>::max()};
  std::size_t fast_queue_capacity{0};
  std::size_t thread_clean_interval{60000};
  std::size_t task_scan_interval{100};
  std::size_t suspend_time{1};
};

struct ServerConfig {
  // Legacy worker-thread count fallback when server.executor is not configured.
  std::size_t thread_count;
  // Optional connection-cap controls (handled at accept/connection lifecycle).
  bool has_max_connection{false};
  std::size_t max_connection{0};
  // Optional worker executor configuration for HttpServer construction.
  ExecutorConfig executor;
  std::vector<ListenerConfig> listeners;
  GlobalConfig global;
  std::vector<RouteConfig> routes;
};

}  // namespace bsrvcore::runtime

#endif
