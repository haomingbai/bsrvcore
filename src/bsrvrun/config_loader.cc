/**
 * @file config_loader.cc
 * @brief YAML config loader for bsrvrun.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "config_loader.h"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace bsrvcore::runtime {

namespace {

bsrvcore::HttpRequestMethod ParseMethod(const YAML::Node& node,
                                        const std::string& context) {
  if (!node || !node.IsScalar()) {
    throw std::runtime_error(context + ": method must be a string");
  }

  static const std::map<std::string, bsrvcore::HttpRequestMethod> kMethodMap = {
      {"GET", bsrvcore::HttpRequestMethod::kGet},
      {"POST", bsrvcore::HttpRequestMethod::kPost},
      {"PUT", bsrvcore::HttpRequestMethod::kPut},
      {"PATCH", bsrvcore::HttpRequestMethod::kPatch},
      {"DELETE", bsrvcore::HttpRequestMethod::kDelete},
      {"HEAD", bsrvcore::HttpRequestMethod::kHead},
  };

  const std::string method = node.as<std::string>();
  const auto it = kMethodMap.find(method);
  if (it != kMethodMap.end()) {
    return it->second;
  }

  throw std::runtime_error(context + ": unsupported method: " + method);
}

FactoryConfig ParseFactoryConfig(const YAML::Node& node,
                                 const std::string& context) {
  if (!node || !node.IsMap()) {
    throw std::runtime_error(context + ": must be an object");
  }

  const YAML::Node factory_node = node["factory"];
  if (!factory_node || !factory_node.IsScalar()) {
    throw std::runtime_error(context + ": field `factory` is required");
  }

  FactoryConfig config;
  config.library = factory_node.as<std::string>();

  const YAML::Node params_node = node["params"];
  if (params_node) {
    if (!params_node.IsMap()) {
      throw std::runtime_error(context + ": field `params` must be an object");
    }
    for (const auto& item : params_node) {
      if (!item.first.IsScalar() || !item.second.IsScalar()) {
        throw std::runtime_error(context +
                                 ": `params` keys and values must be strings");
      }
      config.params.emplace(item.first.as<std::string>(),
                            item.second.as<std::string>());
    }
  }

  return config;
}

std::vector<FactoryConfig> ParseFactoryConfigList(const YAML::Node& node,
                                                  const std::string& context) {
  std::vector<FactoryConfig> result;
  if (!node) {
    return result;
  }

  if (!node.IsSequence()) {
    throw std::runtime_error(context + ": must be an array");
  }

  result.reserve(node.size());
  for (std::size_t i = 0; i < node.size(); ++i) {
    std::ostringstream ctx;
    ctx << context << "[" << i << "]";
    result.emplace_back(ParseFactoryConfig(node[i], ctx.str()));
  }

  return result;
}

std::vector<ListenerConfig> ParseListeners(const YAML::Node& node) {
  if (!node || !node.IsSequence() || node.size() == 0) {
    throw std::runtime_error("listeners must be a non-empty array");
  }

  std::vector<ListenerConfig> listeners;
  listeners.reserve(node.size());
  for (std::size_t i = 0; i < node.size(); ++i) {
    const YAML::Node item = node[i];
    if (!item.IsMap()) {
      throw std::runtime_error("listeners item must be an object");
    }

    const YAML::Node address_node = item["address"];
    const YAML::Node port_node = item["port"];
    if (!address_node || !address_node.IsScalar()) {
      throw std::runtime_error("listeners.address is required");
    }
    if (!port_node || !port_node.IsScalar()) {
      throw std::runtime_error("listeners.port is required");
    }

    const int port = port_node.as<int>();
    if (port <= 0 || port > 65535) {
      throw std::runtime_error("listeners.port must be in range [1, 65535]");
    }

    listeners.push_back(
        {address_node.as<std::string>(), static_cast<std::uint16_t>(port)});
  }

  return listeners;
}

std::vector<RouteConfig> ParseRoutes(const YAML::Node& node) {
  std::vector<RouteConfig> routes;
  if (!node) {
    return routes;
  }

  if (!node.IsSequence()) {
    throw std::runtime_error("routes must be an array");
  }

  routes.reserve(node.size());
  for (std::size_t i = 0; i < node.size(); ++i) {
    const YAML::Node route_node = node[i];
    if (!route_node.IsMap()) {
      throw std::runtime_error("route item must be an object");
    }

    std::ostringstream ctx;
    ctx << "routes[" << i << "]";

    const YAML::Node path_node = route_node["path"];
    if (!path_node || !path_node.IsScalar()) {
      throw std::runtime_error(ctx.str() + ": field `path` is required");
    }

    const YAML::Node ignore_node = route_node["ignore_default_route"];
    const YAML::Node cpu_node = route_node["cpu"];

    RouteConfig route{
        .method = ParseMethod(route_node["method"], ctx.str()),
        .path = path_node.as<std::string>(),
        .ignore_default_route = ignore_node ? ignore_node.as<bool>() : false,
        .cpu = cpu_node ? cpu_node.as<bool>() : false,
        .handler =
            ParseFactoryConfig(route_node["handler"], ctx.str() + ".handler"),
        .aspects = ParseFactoryConfigList(route_node["aspects"],
                                          ctx.str() + ".aspects"),
    };

    routes.emplace_back(std::move(route));
  }

  return routes;
}

GlobalConfig ParseGlobal(const YAML::Node& node) {
  GlobalConfig global;
  if (!node) {
    return global;
  }

  if (!node.IsMap()) {
    throw std::runtime_error("global must be an object");
  }

  const YAML::Node default_handler = node["default_handler"];
  if (default_handler) {
    global.default_handler =
        ParseFactoryConfig(default_handler, "global.default_handler");
  }

  global.aspects = ParseFactoryConfigList(node["aspects"], "global.aspects");
  return global;
}

ExecutorConfig ParseExecutorConfig(const YAML::Node& node) {
  ExecutorConfig config;
  if (!node) {
    return config;
  }

  if (!node.IsMap()) {
    throw std::runtime_error("server.executor must be an object");
  }

  config.configured = true;

  const YAML::Node core_thread_num_node = node["core_thread_num"];
  if (core_thread_num_node) {
    if (!core_thread_num_node.IsScalar()) {
      throw std::runtime_error(
          "server.executor.core_thread_num must be a number");
    }
    config.core_thread_num = core_thread_num_node.as<std::size_t>();
    if (config.core_thread_num == 0) {
      throw std::runtime_error(
          "server.executor.core_thread_num must be greater than 0");
    }
  }

  const YAML::Node max_thread_num_node = node["max_thread_num"];
  if (max_thread_num_node) {
    if (!max_thread_num_node.IsScalar()) {
      throw std::runtime_error(
          "server.executor.max_thread_num must be a number");
    }
    config.max_thread_num = max_thread_num_node.as<std::size_t>();
    if (config.max_thread_num == 0) {
      throw std::runtime_error(
          "server.executor.max_thread_num must be greater than 0");
    }
  }

  if (config.max_thread_num < config.core_thread_num) {
    throw std::runtime_error(
        "server.executor.max_thread_num must be greater than or equal to "
        "core_thread_num");
  }

  const YAML::Node fast_queue_capacity_node = node["fast_queue_capacity"];
  if (fast_queue_capacity_node) {
    if (!fast_queue_capacity_node.IsScalar()) {
      throw std::runtime_error(
          "server.executor.fast_queue_capacity must be a number");
    }
    config.fast_queue_capacity = fast_queue_capacity_node.as<std::size_t>();
  }

  const YAML::Node thread_clean_interval_node = node["thread_clean_interval"];
  if (thread_clean_interval_node) {
    if (!thread_clean_interval_node.IsScalar()) {
      throw std::runtime_error(
          "server.executor.thread_clean_interval must be a number");
    }
    config.thread_clean_interval = thread_clean_interval_node.as<std::size_t>();
  }

  const YAML::Node task_scan_interval_node = node["task_scan_interval"];
  if (task_scan_interval_node) {
    if (!task_scan_interval_node.IsScalar()) {
      throw std::runtime_error(
          "server.executor.task_scan_interval must be a number");
    }
    config.task_scan_interval = task_scan_interval_node.as<std::size_t>();
  }

  const YAML::Node suspend_time_node = node["suspend_time"];
  if (suspend_time_node) {
    if (!suspend_time_node.IsScalar()) {
      throw std::runtime_error("server.executor.suspend_time must be a number");
    }
    config.suspend_time = suspend_time_node.as<std::size_t>();
  }

  return config;
}

}  // namespace

std::string ResolveConfigPath(const std::optional<std::string>& cli_path) {
  if (cli_path.has_value()) {
    const std::filesystem::path configured_path(*cli_path);
    if (!std::filesystem::exists(configured_path)) {
      throw std::runtime_error("config file not found: " +
                               configured_path.string());
    }
    return configured_path.string();
  }

  const std::filesystem::path cwd_default =
      std::filesystem::current_path() / "bsrvrun.yaml";
  if (std::filesystem::exists(cwd_default)) {
    return cwd_default.string();
  }

  const std::filesystem::path etc_default = "/etc/bsrvrun/bsrvrun.yaml";
  if (std::filesystem::exists(etc_default)) {
    return etc_default.string();
  }

  throw std::runtime_error(
      "cannot find config file; checked ./bsrvrun.yaml and "
      "/etc/bsrvrun/bsrvrun.yaml");
}

ServerConfig LoadConfigFromFile(const std::string& path) {
  const YAML::Node root = YAML::LoadFile(path);
  if (!root || !root.IsMap()) {
    throw std::runtime_error("config root must be an object");
  }

  std::size_t thread_count = 4;
  bool has_max_connection = false;
  std::size_t max_connection = 0;
  const YAML::Node server_node = root["server"];
  if (server_node) {
    if (!server_node.IsMap()) {
      throw std::runtime_error("server must be an object");
    }
    const YAML::Node thread_count_node = server_node["thread_count"];
    if (thread_count_node) {
      if (!thread_count_node.IsScalar()) {
        throw std::runtime_error("server.thread_count must be a number");
      }
      thread_count = thread_count_node.as<std::size_t>();
      if (thread_count == 0) {
        throw std::runtime_error("server.thread_count must be greater than 0");
      }
    }

    const YAML::Node has_max_connection_node =
        server_node["has_max_connection"];
    if (has_max_connection_node) {
      if (!has_max_connection_node.IsScalar()) {
        throw std::runtime_error("server.has_max_connection must be a bool");
      }
      has_max_connection = has_max_connection_node.as<bool>();
    }

    const YAML::Node max_connection_node = server_node["max_connection"];
    if (max_connection_node) {
      if (!max_connection_node.IsScalar()) {
        throw std::runtime_error("server.max_connection must be a number");
      }
      const auto max_connection_value = max_connection_node.as<long long>();
      if (max_connection_value <= 0) {
        throw std::runtime_error(
            "server.max_connection must be greater than 0");
      }
      max_connection = static_cast<std::size_t>(max_connection_value);
    }
  }

  if (has_max_connection && max_connection == 0) {
    throw std::runtime_error(
        "server.max_connection must be greater than 0 when "
        "server.has_max_connection is true");
  }

  ExecutorConfig executor =
      ParseExecutorConfig(server_node ? server_node["executor"] : YAML::Node{});

  ServerConfig config{
      .thread_count = thread_count,
      .has_max_connection = has_max_connection,
      .max_connection = max_connection,
      .executor = std::move(executor),
      .listeners = ParseListeners(root["listeners"]),
      .global = ParseGlobal(root["global"]),
      .routes = ParseRoutes(root["routes"]),
  };

  return config;
}

}  // namespace bsrvcore::runtime
