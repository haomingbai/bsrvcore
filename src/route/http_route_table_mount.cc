/**
 * @file http_route_table_mount.cc
 * @brief Tree merge and mount logic for HttpRouteTable.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/internal/route/http_route_table.h"
#include "bsrvcore/route/cloneable_http_request_aspect_handler.h"
#include "bsrvcore/route/cloneable_http_request_handler.h"
#include "impl/http_route_target_validator.h"
#include "internal/http_route_table_detail.h"
#include "internal/http_route_table_layer.h"

using bsrvcore::HttpRouteTable;
using bsrvcore::route_internal::HttpRouteTableLayer;

namespace bsrvcore {

std::string HttpRouteTable::JoinRouteTemplate(std::string_view prefix,
                                              std::string_view route_template) {
  std::string normalized_prefix = NormalizeRouteTemplate(prefix);
  std::string normalized_route = NormalizeRouteTemplate(route_template);

  if (normalized_prefix.size() > 1 && normalized_prefix.back() == '/') {
    normalized_prefix.pop_back();
  }
  if (normalized_prefix == "/") {
    return normalized_route;
  }
  if (normalized_route == "/") {
    return normalized_prefix;
  }
  return normalized_prefix + normalized_route;
}

void HttpRouteTable::PrefixRouteTemplates(HttpRouteTableLayer& layer,
                                          std::string_view prefix) {
  if (HasTerminalConfiguration(layer)) {
    // Only layers that actually terminate behavior need user-visible route
    // templates. Pure traversal nodes keep empty templates.
    layer.route_template_ = JoinRouteTemplate(prefix, layer.route_template_);
  }

  if (layer.default_route_ != nullptr) {
    PrefixRouteTemplates(*layer.default_route_, prefix);
  }
  for (auto& [key, child] : layer.map_) {
    (void)key;
    if (child != nullptr) {
      PrefixRouteTemplates(*child, prefix);
    }
  }
}

bool HttpRouteTable::CanMergeLayer(const HttpRouteTableLayer& dst,
                                   const HttpRouteTableLayer& src) noexcept {
  if (HasTerminalConfiguration(src) && HasTerminalConfiguration(dst)) {
    // Mounting is rejected when both trees already define behavior at the same
    // logical node; this keeps MountAt() from silently overwriting handlers or
    // route-local policies.
    return false;
  }

  if (src.default_route_ != nullptr && dst.default_route_ != nullptr &&
      !CanMergeLayer(*dst.default_route_, *src.default_route_)) {
    return false;
  }

  for (const auto& [key, src_child] : src.map_) {
    if (src_child == nullptr) {
      continue;
    }

    auto dst_it = dst.map_.find(key);
    if (dst_it == dst.map_.end() || dst_it->second == nullptr) {
      continue;
    }

    if (!CanMergeLayer(*dst_it->second, *src_child)) {
      return false;
    }
  }

  return true;
}

void HttpRouteTable::MoveMergeLayer(HttpRouteTableLayer& dst,
                                    HttpRouteTableLayer& src) {
  if (src.handler_ != nullptr) {
    dst.handler_ = std::move(src.handler_);
    dst.param_names_ = std::move(src.param_names_);
    dst.route_template_ = std::move(src.route_template_);
  }

  if (!src.aspects_.empty()) {
    dst.aspects_ = std::move(src.aspects_);
  }
  if (src.max_body_size_ != 0) {
    dst.max_body_size_ = src.max_body_size_;
  }
  if (src.read_expiry_ != 0) {
    dst.read_expiry_ = src.read_expiry_;
  }
  if (src.write_expiry_ != 0) {
    dst.write_expiry_ = src.write_expiry_;
  }
  if (src.ignore_default_route_) {
    dst.ignore_default_route_ = true;
  }

  if (src.default_route_ != nullptr) {
    if (dst.default_route_ == nullptr) {
      dst.default_route_ = std::move(src.default_route_);
    } else {
      // Child recursion is safe here because CanMergeLayer() already performed
      // a full conflict preflight before any state mutation started.
      MoveMergeLayer(*dst.default_route_, *src.default_route_);
      src.default_route_.reset();
    }
  }

  for (auto& [key, src_child] : src.map_) {
    if (src_child == nullptr) {
      continue;
    }

    auto dst_it = dst.map_.find(key);
    if (dst_it == dst.map_.end() || dst_it->second == nullptr) {
      dst.map_.emplace(key, std::move(src_child));
      continue;
    }
    MoveMergeLayer(*dst_it->second, *src_child);
  }
  src.map_.clear();
}

bool HttpRouteTable::CloneLayer(const HttpRouteTableLayer& src,
                                OwnedPtr<HttpRouteTableLayer>& dst) {
  auto cloned = AllocateUnique<HttpRouteTableLayer>();
  cloned->max_body_size_ = src.max_body_size_;
  cloned->read_expiry_ = src.read_expiry_;
  cloned->write_expiry_ = src.write_expiry_;
  cloned->ignore_default_route_ = src.ignore_default_route_;
  cloned->param_names_ = src.param_names_;
  cloned->route_template_ = src.route_template_;

  if (src.handler_ != nullptr) {
    // Clone-based mounting is only available for handlers/aspects that opt into
    // the cloneable interfaces; runtime-only handler instances cannot be
    // duplicated safely.
    auto* cloneable_handler =
        dynamic_cast<const CloneableHttpRequestHandler*>(src.handler_.get());
    if (cloneable_handler == nullptr) {
      return false;
    }
    cloned->handler_ = cloneable_handler->Clone();
  }

  cloned->aspects_.reserve(src.aspects_.size());
  for (const auto& aspect : src.aspects_) {
    auto* cloneable_aspect =
        dynamic_cast<const CloneableHttpRequestAspectHandler*>(aspect.get());
    if (cloneable_aspect == nullptr) {
      return false;
    }
    cloned->aspects_.emplace_back(cloneable_aspect->Clone());
  }

  if (src.default_route_ != nullptr &&
      !CloneLayer(*src.default_route_, cloned->default_route_)) {
    return false;
  }

  for (const auto& [key, child] : src.map_) {
    if (child == nullptr) {
      continue;
    }

    OwnedPtr<HttpRouteTableLayer> cloned_child;
    if (!CloneLayer(*child, cloned_child)) {
      return false;
    }
    cloned->map_.emplace(key, std::move(cloned_child));
  }

  dst = std::move(cloned);
  return true;
}

bool HttpRouteTable::MountAt(std::string_view prefix, HttpRouteTable&& source) {
  if (!route_internal::IsValidParametricTarget(prefix)) {
    return false;
  }

  const auto get_or_create_mount_layer =
      [&](HttpRouteTableLayer* route_layer) -> HttpRouteTableLayer* {
    for (auto word : route_internal::detail::SplitTargetSegments(prefix)) {
      if (route_internal::detail::IsParameterSegment(word)) {
        if (route_layer->default_route_ == nullptr) {
          route_layer->default_route_ = AllocateUnique<HttpRouteTableLayer>();
        }
        route_layer = route_layer->default_route_.get();
        continue;
      }

      auto it = route_layer->map_.find(std::string(word));
      if (it == route_layer->map_.end()) {
        auto new_layer = AllocateUnique<HttpRouteTableLayer>();
        auto* next = new_layer.get();
        route_layer->map_.emplace(std::string(word), std::move(new_layer));
        route_layer = next;
      } else {
        route_layer = it->second.get();
      }
    }

    return route_layer;
  };

  for (std::size_t method_idx = 0; method_idx < entrance_.size();
       ++method_idx) {
    PrefixRouteTemplates(*source.entrance_[method_idx], prefix);

    auto* mount_root = get_or_create_mount_layer(entrance_[method_idx].get());
    // First pass is validation only. Either every method tree can merge cleanly
    // or the mount leaves the destination untouched.
    if (!CanMergeLayer(*mount_root, *source.entrance_[method_idx])) {
      return false;
    }
  }

  for (std::size_t method_idx = 0; method_idx < entrance_.size();
       ++method_idx) {
    auto* mount_root = get_or_create_mount_layer(entrance_[method_idx].get());
    MoveMergeLayer(*mount_root, *source.entrance_[method_idx]);
  }

  return true;
}

bool HttpRouteTable::MountAt(std::string_view prefix,
                             const HttpRouteTable& source) {
  if (!route_internal::IsValidParametricTarget(prefix)) {
    return false;
  }

  HttpRouteTable cloned_source;
  for (std::size_t method_idx = 0; method_idx < entrance_.size();
       ++method_idx) {
    if (!CloneLayer(*source.entrance_[method_idx],
                    cloned_source.entrance_[method_idx])) {
      return false;
    }
  }

  return MountAt(prefix, std::move(cloned_source));
}

}  // namespace bsrvcore
