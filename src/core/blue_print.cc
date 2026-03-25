/**
 * @file blue_print.cc
 * @brief BluePrint and ReuseableBluePrint implementation.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-26
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "bsrvcore/blue_print.h"

#include <memory>
#include <utility>

#include "bsrvcore/internal/http_route_table.h"

namespace bsrvcore {

class BluePrint::Impl {
 public:
  Impl() : route_table_(AllocateUnique<HttpRouteTable>()) {}

  OwnedPtr<HttpRouteTable> route_table_;
};

class ReuseableBluePrint::Impl {
 public:
  Impl() : route_table_(AllocateUnique<HttpRouteTable>()) {}

  OwnedPtr<HttpRouteTable> route_table_;
};

BluePrint::BluePrint() : impl_(AllocateUnique<Impl>()) {}

BluePrint::BluePrint(BluePrint&&) noexcept = default;

BluePrint& BluePrint::operator=(BluePrint&&) noexcept = default;

BluePrint::~BluePrint() = default;

BluePrint* BluePrint::AddRouteEntry(HttpRequestMethod method,
                                    std::string_view url,
                                    OwnedPtr<HttpRequestHandler> handler) {
  impl_->route_table_->AddRouteEntry(method, url, std::move(handler));
  return this;
}

BluePrint* BluePrint::AddExclusiveRouteEntry(
    HttpRequestMethod method, std::string_view url,
    OwnedPtr<HttpRequestHandler> handler) {
  impl_->route_table_->AddExclusiveRouteEntry(method, url, std::move(handler));
  return this;
}

BluePrint* BluePrint::AddAspect(HttpRequestMethod method, std::string_view url,
                                OwnedPtr<HttpRequestAspectHandler> aspect) {
  impl_->route_table_->AddAspect(method, url, std::move(aspect));
  return this;
}

BluePrint* BluePrint::SetReadExpiry(HttpRequestMethod method,
                                    std::string_view url,
                                    std::size_t expiry) {
  impl_->route_table_->SetReadExpiry(method, url, expiry);
  return this;
}

BluePrint* BluePrint::SetWriteExpiry(HttpRequestMethod method,
                                     std::string_view url,
                                     std::size_t expiry) {
  impl_->route_table_->SetWriteExpiry(method, url, expiry);
  return this;
}

BluePrint* BluePrint::SetMaxBodySize(HttpRequestMethod method,
                                     std::string_view url, std::size_t size) {
  impl_->route_table_->SetMaxBodySize(method, url, size);
  return this;
}

bool BluePrint::MountInto(std::string_view prefix,
                          HttpRouteTable& route_table) && {
  if (impl_ == nullptr || impl_->route_table_ == nullptr) {
    return false;
  }
  return route_table.MountAt(prefix, std::move(*impl_->route_table_));
}

ReuseableBluePrint::ReuseableBluePrint() : impl_(AllocateUnique<Impl>()) {}

ReuseableBluePrint::ReuseableBluePrint(ReuseableBluePrint&&) noexcept = default;

ReuseableBluePrint& ReuseableBluePrint::operator=(
    ReuseableBluePrint&&) noexcept = default;

ReuseableBluePrint::~ReuseableBluePrint() = default;

ReuseableBluePrint* ReuseableBluePrint::AddRouteEntry(
    HttpRequestMethod method, std::string_view url,
    OwnedPtr<CloneableHttpRequestHandler> handler) {
  impl_->route_table_->AddRouteEntry(method, url, std::move(handler));
  return this;
}

ReuseableBluePrint* ReuseableBluePrint::AddExclusiveRouteEntry(
    HttpRequestMethod method, std::string_view url,
    OwnedPtr<CloneableHttpRequestHandler> handler) {
  impl_->route_table_->AddExclusiveRouteEntry(method, url, std::move(handler));
  return this;
}

ReuseableBluePrint* ReuseableBluePrint::AddAspect(
    HttpRequestMethod method, std::string_view url,
    OwnedPtr<CloneableHttpRequestAspectHandler> aspect) {
  impl_->route_table_->AddAspect(method, url, std::move(aspect));
  return this;
}

ReuseableBluePrint* ReuseableBluePrint::SetReadExpiry(
    HttpRequestMethod method, std::string_view url, std::size_t expiry) {
  impl_->route_table_->SetReadExpiry(method, url, expiry);
  return this;
}

ReuseableBluePrint* ReuseableBluePrint::SetWriteExpiry(
    HttpRequestMethod method, std::string_view url, std::size_t expiry) {
  impl_->route_table_->SetWriteExpiry(method, url, expiry);
  return this;
}

ReuseableBluePrint* ReuseableBluePrint::SetMaxBodySize(
    HttpRequestMethod method, std::string_view url, std::size_t size) {
  impl_->route_table_->SetMaxBodySize(method, url, size);
  return this;
}

bool ReuseableBluePrint::MountInto(std::string_view prefix,
                                   HttpRouteTable& route_table) const {
  if (impl_ == nullptr || impl_->route_table_ == nullptr) {
    return false;
  }
  return route_table.MountAt(prefix, *impl_->route_table_);
}

BluePrint BluePrintFactory::Create() { return BluePrint(); }

ReuseableBluePrint BluePrintFactory::CreateReuseable() {
  return ReuseableBluePrint();
}

}  // namespace bsrvcore
