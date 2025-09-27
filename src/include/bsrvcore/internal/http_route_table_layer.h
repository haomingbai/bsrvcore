/**
 * @file http_route_table_layer.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-25
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_HTTP_ROUTE_TABLE_LAYER_H_
#define BSRVCORE_INTERNAL_HTTP_ROUTE_TABLE_LAYER_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "bsrvcore/trait.h"

namespace bsrvcore {

class HttpRequestHandler;

namespace route_internal {

class HttpRouteTableLayer : NonCopyableNonMovable<HttpRouteTableLayer> {
 public:
  HttpRouteTableLayer(HttpRouteTableLayer&&) = delete;

  HttpRouteTableLayer(const HttpRouteTableLayer&) = delete;

  HttpRouteTableLayer& operator=(HttpRouteTableLayer&&) = delete;

  HttpRouteTableLayer& operator=(const HttpRouteTableLayer&) = delete;

  HttpRouteTableLayer();

  void SetMaxBodySize(std::size_t max_body_size) noexcept;

  void SetReadExpiry(std::size_t expiry) noexcept;

  void SetWriteExpiry(std::size_t expiry) noexcept;

  bool SetHandler(std::unique_ptr<HttpRequestHandler> handler) noexcept;

  bool SetDefaulltRoute(std::unique_ptr<HttpRouteTableLayer> route) noexcept;

  bool SetRoute(std::string key, std::unique_ptr<HttpRouteTableLayer> link);

  void SetIgnoreDefaultRoute(bool flag) noexcept;

  HttpRouteTableLayer* GetDefaultRoute() noexcept;

  HttpRouteTableLayer* GetRoute(const std::string& key) noexcept;

  HttpRequestHandler* GetHanndler() noexcept;

  bool GetIgnoreDefaultRoute() noexcept;

 private:
  std::unordered_map<std::string, std::unique_ptr<HttpRouteTableLayer>> map_;
  std::unique_ptr<HttpRouteTableLayer> default_route_;
  std::unique_ptr<HttpRequestHandler> handler_;
  std::size_t max_body_size_;
  std::size_t read_expiry_;
  std::size_t write_expiry_;
  bool ignore_default_route_;
};

}  // namespace route_internal

}  // namespace bsrvcore

#endif
