/**
 * @file http_route_table.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-24
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_HTTP_ROUTE_TABLE_H_
#define BSRVCORE_INTERNAL_HTTP_ROUTE_TABLE_H_

#include <array>
#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "bsrvcore/http_request_method.h"
#include "bsrvcore/internal/http_route_table_layer.h"
#include "bsrvcore/trait.h"

namespace bsrvcore {

class HttpRequestHandler;

struct HttpRouteResult {
  HttpRequestHandler* handler;
  std::string current_location;
  std::size_t max_body_size;
  std::size_t read_expiry;
  std::size_t write_expiry;
};

class HttpRouteTable : NonCopyableNonMovable<HttpRouteTable> {
 public:
  HttpRouteTable& operator=(const HttpRouteTable&) = delete;

  HttpRouteTable() noexcept;

  HttpRouteResult Route(std::string_view) noexcept;

  bool AddRouteEntry(HttpRequestMethod method, std::string_view target,
                     std::unique_ptr<HttpRequestHandler> handler);

  bool AddExclusiveRouteEntry(HttpRequestMethod method, std::string_view target,
                              std::unique_ptr<HttpRequestHandler> handler);

  bool SetReadExpiry(HttpRequestMethod method, std::string_view target,
                     std::size_t expiry) noexcept;

  bool SetWriteExpiry(HttpRequestMethod method, std::string_view target,
                      std::size_t expiry) noexcept;

  bool SetMaxBodySize(HttpRequestMethod method, std::string_view target,
                      std::size_t max_body_size) noexcept;

  void SetDefaultReadExpiry(std::size_t expiry) noexcept;

  void SetDefaultWriteExpiry(std::size_t expiry) noexcept;

  void SetDefaultMaxBodySize(std::size_t max_body_size) noexcept;

 private:
  static constexpr size_t kHttpRequestMethodNum = 9;
  std::shared_mutex mtx_;
  std::array<std::unique_ptr<route_internal::HttpRouteTableLayer>,
             kHttpRequestMethodNum>
      entrance_;
  std::unique_ptr<route_internal::HttpRouteTableLayer> default_handler_;
  std::size_t default_max_body_size_;
  std::size_t default_read_expiry_;
  std::size_t default_write_expriy;
};

}  // namespace bsrvcore

#endif
