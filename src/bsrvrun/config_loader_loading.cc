/**
 * @file config_loader_loading.cc
 * @brief File lookup and top-level assembly for bsrvrun config loading.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "config_loader.h"
#include "config_loader_detail.h"
#include "config_types.h"

namespace bsrvcore::runtime {

std::string ResolveConfigPath(const std::optional<std::string>& cli_path) {
  if (cli_path.has_value()) {
    // Explicit CLI input always wins; missing files are treated as operator
    // mistakes instead of silently falling back to another location.
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

  // Top-level loading only coordinates the individual parsers. Keeping the
  // assembly step small makes it easier to verify what defaults come from each
  // section and where validation errors originate.
  const auto server = config_loader_detail::ParseServerSection(root["server"]);

  // Top-level assembly is intentionally a thin projection step: every nested
  // parser owns its validation rules so error messages still point at the
  // originating section.
  ServerConfig config{
      .thread_count = server.thread_count,
      .has_max_connection = server.has_max_connection,
      .max_connection = server.max_connection,
      .executor = server.executor,
      .listeners = config_loader_detail::ParseListeners(root["listeners"]),
      .global = config_loader_detail::ParseGlobal(root["global"]),
      .routes = config_loader_detail::ParseRoutes(root["routes"]),
  };

  return config;
}

}  // namespace bsrvcore::runtime
