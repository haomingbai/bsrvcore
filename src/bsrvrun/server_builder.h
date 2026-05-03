/**
 * @file server_builder.h
 * @brief Apply bsrvrun config to HttpServer.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_SERVER_BUILDER_H_
#define BSRVCORE_BSRVRUN_SERVER_BUILDER_H_

namespace bsrvcore {
class HttpServer;
namespace runtime {
class PluginLoader;
struct ServerConfig;
}  // namespace runtime
}  // namespace bsrvcore

namespace bsrvcore::runtime {

void ApplyConfigToServer(const ServerConfig& config, PluginLoader* loader,
                         bsrvcore::HttpServer* server);

}  // namespace bsrvcore::runtime

#endif
