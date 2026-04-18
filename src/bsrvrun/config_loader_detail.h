/**
 * @file config_loader_detail.h
 * @brief Internal parsing helpers shared by bsrvrun config-loader units.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_CONFIG_LOADER_DETAIL_H_
#define BSRVCORE_BSRVRUN_CONFIG_LOADER_DETAIL_H_

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "config_types.h"

namespace bsrvcore::runtime::config_loader_detail {

struct ServerSectionConfig {
  std::size_t thread_count{4};
  bool has_max_connection{false};
  std::size_t max_connection{0};
  ExecutorConfig executor;
};

bsrvcore::HttpRequestMethod ParseMethod(const YAML::Node& node,
                                        const std::string& context);

FactoryConfig ParseFactoryConfig(const YAML::Node& node,
                                 const std::string& context);

std::vector<FactoryConfig> ParseFactoryConfigList(const YAML::Node& node,
                                                  const std::string& context);

std::vector<ListenerConfig> ParseListeners(const YAML::Node& node);

std::vector<ServiceConfig> ParseServices(const YAML::Node& node);

std::optional<FactoryConfig> ParseLogger(const YAML::Node& node);

std::vector<RouteConfig> ParseRoutes(const YAML::Node& node);

GlobalConfig ParseGlobal(const YAML::Node& node);

ExecutorConfig ParseExecutorConfig(const YAML::Node& node);

ServerSectionConfig ParseServerSection(const YAML::Node& node);

}  // namespace bsrvcore::runtime::config_loader_detail

#endif
