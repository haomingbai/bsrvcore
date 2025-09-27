/**
 * @file context.h
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

#ifndef BSRVCORE_CONTEXT_H_
#define BSRVCORE_CONTEXT_H_

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "bsrvcore/trait.h"

namespace bsrvcore {
class Attribute;

class Context : NonCopyableNonMovable<Context> {
 public:
  std::shared_ptr<Attribute> GetAttribute(const std::string &key) noexcept;

  std::shared_ptr<Attribute> GetAttribute(std::string &&key) noexcept;

  void SetAttribute(std::string key, std::shared_ptr<Attribute> val);

  bool HasAttribute(const std::string &key) noexcept;

  bool HasAttribute(std::string &&key) noexcept;

  Context() = default;

 private:
  std::shared_mutex mtx_;
  std::unordered_map<std::string, std::shared_ptr<Attribute>> map_;
};
}  // namespace bsrvcore

#endif
