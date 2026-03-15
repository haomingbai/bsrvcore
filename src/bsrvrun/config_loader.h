/**
 * @file config_loader.h
 * @brief YAML config loader for bsrvrun.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_CONFIG_LOADER_H_
#define BSRVCORE_BSRVRUN_CONFIG_LOADER_H_

#include <optional>
#include <string>

#include "config_types.h"

namespace bsrvcore::runtime {

std::string ResolveConfigPath(const std::optional<std::string>& cli_path);

ServerConfig LoadConfigFromFile(const std::string& path);

}  // namespace bsrvcore::runtime

#endif
