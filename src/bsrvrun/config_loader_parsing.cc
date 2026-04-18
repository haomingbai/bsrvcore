/**
 * @file config_loader_parsing.cc
 * @brief YAML-to-config parsing helpers for bsrvrun.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bsrvcore/route/http_request_method.h"
#include "config_loader_detail.h"
#include "config_types.h"
#include "yaml-cpp/node/node.h"

namespace bsrvcore::runtime::config_loader_detail {

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
  if (!params_node) {
    return config;
  }

  if (!params_node.IsMap()) {
    throw std::runtime_error(context + ": field `params` must be an object");
  }

  for (const auto& item : params_node) {
    if (!item.first.IsScalar() || !item.second.IsScalar()) {
      throw std::runtime_error(context +
                               ": `params` keys and values must be strings");
    }
    // Runtime plugins receive a string-only ParameterMap, so YAML parsing keeps
    // that contract explicit here instead of inventing implicit type coercions.
    config.params.emplace(item.first.as<std::string>(),
                          item.second.as<std::string>());
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
  for (const auto& i : node) {
    const YAML::Node item = i;
    if (!item.IsMap()) {
      throw std::runtime_error("listeners item must be an object");
    }

    const YAML::Node address_node = item["address"];
    const YAML::Node port_node = item["port"];
    const YAML::Node io_threads_node = item["io_threads"];
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

    std::size_t io_threads = 1;
    if (io_threads_node) {
      if (!io_threads_node.IsScalar()) {
        throw std::runtime_error("listeners.io_threads must be a number");
      }
      io_threads = io_threads_node.as<std::size_t>();
      if (io_threads == 0) {
        throw std::runtime_error("listeners.io_threads must be greater than 0");
      }
    }

    listeners.push_back({address_node.as<std::string>(),
                         static_cast<std::uint16_t>(port), io_threads});
  }

  return listeners;
}

std::vector<ServiceConfig> ParseServices(const YAML::Node& node) {
  std::vector<ServiceConfig> services;
  if (!node) {
    return services;
  }

  if (!node.IsSequence()) {
    throw std::runtime_error("services must be an array");
  }

  services.reserve(node.size());
  std::unordered_set<std::size_t> seen_slots;
  for (std::size_t i = 0; i < node.size(); ++i) {
    const YAML::Node service_node = node[i];
    if (!service_node.IsMap()) {
      throw std::runtime_error("service item must be an object");
    }

    std::ostringstream ctx;
    ctx << "services[" << i << "]";

    const YAML::Node slot_node = service_node["slot"];
    if (!slot_node || !slot_node.IsScalar()) {
      throw std::runtime_error(ctx.str() + ": field `slot` is required");
    }

    std::int64_t slot = 0;
    try {
      slot = slot_node.as<std::int64_t>();
    } catch (const YAML::Exception&) {
      throw std::runtime_error(ctx.str() + ": field `slot` must be an integer");
    }
    if (slot < 0) {
      throw std::runtime_error(ctx.str() +
                               ": field `slot` must be greater than or equal "
                               "to 0");
    }

    const std::size_t slot_index = static_cast<std::size_t>(slot);
    if (!seen_slots.insert(slot_index).second) {
      throw std::runtime_error(ctx.str() + ": duplicate service slot " +
                               std::to_string(slot_index));
    }

    services.push_back(ServiceConfig{
        .slot = slot_index,
        .factory = ParseFactoryConfig(service_node, ctx.str()),
    });
  }

  return services;
}

std::optional<FactoryConfig> ParseLogger(const YAML::Node& node) {
  if (!node) {
    return std::nullopt;
  }

  return ParseFactoryConfig(node, "logger");
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

    // These booleans map directly onto server builder decisions:
    // ignore_default_route -> AddExclusiveRouteEntry()
    // cpu -> AddComputingRouteEntry()-style wrapping.
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

  // Leaving a field absent means "inherit library defaults". The parser only
  // validates fields the operator actually overrides in YAML.
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

ServerSectionConfig ParseServerSection(const YAML::Node& node) {
  // The server block is optional. This helper centralizes its defaults and
  // validation so file loading can stay focused on assembling the final model.
  ServerSectionConfig config;
  if (!node) {
    return config;
  }

  if (!node.IsMap()) {
    throw std::runtime_error("server must be an object");
  }

  const YAML::Node thread_count_node = node["thread_count"];
  if (thread_count_node) {
    if (!thread_count_node.IsScalar()) {
      throw std::runtime_error("server.thread_count must be a number");
    }
    config.thread_count = thread_count_node.as<std::size_t>();
    if (config.thread_count == 0) {
      throw std::runtime_error("server.thread_count must be greater than 0");
    }
  }

  const YAML::Node has_max_connection_node = node["has_max_connection"];
  if (has_max_connection_node) {
    if (!has_max_connection_node.IsScalar()) {
      throw std::runtime_error("server.has_max_connection must be a bool");
    }
    config.has_max_connection = has_max_connection_node.as<bool>();
  }

  const YAML::Node max_connection_node = node["max_connection"];
  if (max_connection_node) {
    if (!max_connection_node.IsScalar()) {
      throw std::runtime_error("server.max_connection must be a number");
    }

    const auto max_connection_value = max_connection_node.as<long long>();
    if (max_connection_value <= 0) {
      throw std::runtime_error("server.max_connection must be greater than 0");
    }
    config.max_connection = static_cast<std::size_t>(max_connection_value);
  }

  if (config.has_max_connection && config.max_connection == 0) {
    throw std::runtime_error(
        "server.max_connection must be greater than 0 when "
        "server.has_max_connection is true");
  }

  config.executor = ParseExecutorConfig(node["executor"]);
  return config;
}

}  // namespace bsrvcore::runtime::config_loader_detail
