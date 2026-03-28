/**
 * @file server_builder.cc
 * @brief Apply bsrvrun config to HttpServer.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "server_builder.h"

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <stdexcept>

#include "bsrvcore/core/http_server.h"

namespace bsrvcore::runtime {

void ApplyConfigToServer(const ServerConfig& config, PluginLoader* loader,
                         bsrvcore::HttpServer* server) {
  if (loader == nullptr) {
    throw std::runtime_error("loader cannot be nullptr");
  }
  if (server == nullptr) {
    throw std::runtime_error("server cannot be nullptr");
  }

  for (const auto& listener : config.listeners) {
    const auto addr = boost::asio::ip::make_address(listener.address);
    server->AddListen(boost::asio::ip::tcp::endpoint(addr, listener.port));
  }

  if (config.global.default_handler.has_value()) {
    server->SetDefaultHandler(
        loader->CreateHandler(*config.global.default_handler));
  }

  for (const auto& aspect_config : config.global.aspects) {
    server->AddGlobalAspect(loader->CreateAspect(aspect_config));
  }

  for (const auto& route : config.routes) {
    auto route_handler = loader->CreateHandler(route.handler);
    if (route.ignore_default_route) {
      server->AddExclusiveRouteEntry(route.method, route.path,
                                     std::move(route_handler));
    } else {
      server->AddRouteEntry(route.method, route.path, std::move(route_handler));
    }

    for (const auto& aspect_config : route.aspects) {
      server->AddAspect(route.method, route.path,
                        loader->CreateAspect(aspect_config));
    }
  }
}

}  // namespace bsrvcore::runtime
