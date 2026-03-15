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
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "bsrvcore/http_request_method.h"

namespace bsrvcore::runtime {

struct FactoryConfig {
  std::string library;
  std::unordered_map<std::string, std::string> params;
};

struct ListenerConfig {
  std::string address;
  std::uint16_t port;
};

struct RouteConfig {
  bsrvcore::HttpRequestMethod method;
  std::string path;
  bool ignore_default_route;
  FactoryConfig handler;
  std::vector<FactoryConfig> aspects;
};

struct GlobalConfig {
  std::optional<FactoryConfig> default_handler;
  std::vector<FactoryConfig> aspects;
};

struct ServerConfig {
  std::size_t thread_count;
  std::vector<ListenerConfig> listeners;
  GlobalConfig global;
  std::vector<RouteConfig> routes;
};

}  // namespace bsrvcore::runtime

#endif
