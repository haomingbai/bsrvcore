/**
 * @file parameter_map.h
 * @brief Runtime parameter map abstraction for config-driven factories.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_PARAMETER_MAP_H_
#define BSRVCORE_BSRVRUN_PARAMETER_MAP_H_

#include "bsrvcore/bsrvrun/string.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore::bsrvrun {

/**
 * @brief Key-value parameter map interface consumed by runtime factories.
 */
class ParameterMap : public bsrvcore::NonCopyableNonMovable<ParameterMap> {
 public:
  /** @brief Return the value associated with one key. */
  [[nodiscard]] virtual String Get(const String& key) const = 0;
  /** @brief Set or replace one key-value pair. */
  virtual void Set(const String& key, const String& value) = 0;

  virtual ~ParameterMap() = default;
};

}  // namespace bsrvcore::bsrvrun

#endif
