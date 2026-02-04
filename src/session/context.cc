/**
 * @file context.cc
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-02-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#include "bsrvcore/context.h"

#include <shared_mutex>
#include <mutex>
#include <string>
#include <utility>

#include "bsrvcore/attribute.h"

using bsrvcore::Context;

std::shared_ptr<bsrvcore::Attribute> Context::GetAttribute(
    const std::string& key) noexcept {
  std::shared_lock<std::shared_mutex> lock(mtx_);
  auto it = map_.find(key);
  if (it == map_.end()) {
    return nullptr;
  }
  return it->second;
}

std::shared_ptr<bsrvcore::Attribute> Context::GetAttribute(
    std::string&& key) noexcept {
  std::shared_lock<std::shared_mutex> lock(mtx_);
  auto it = map_.find(key);
  if (it == map_.end()) {
    return nullptr;
  }
  return it->second;
}

void Context::SetAttribute(std::string key,
                           std::shared_ptr<bsrvcore::Attribute> val) {
  std::unique_lock<std::shared_mutex> lock(mtx_);
  map_[std::move(key)] = std::move(val);
}

bool Context::HasAttribute(const std::string& key) noexcept {
  std::shared_lock<std::shared_mutex> lock(mtx_);
  return map_.count(key) > 0;
}

bool Context::HasAttribute(std::string&& key) noexcept {
  std::shared_lock<std::shared_mutex> lock(mtx_);
  return map_.count(key) > 0;
}
