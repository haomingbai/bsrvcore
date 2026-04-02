/**
 * @file parameter_map_impl.cc
 * @brief ParameterMap implementation for bsrvrun runtime.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "parameter_map_impl.h"

#include <string>

#include "bsrvcore/bsrvrun/string.h"

namespace bsrvcore::runtime {

bsrvcore::bsrvrun::String RuntimeParameterMap::Get(
    const bsrvcore::bsrvrun::String& key) const {
  const auto it = map_.find(key.ToStdString());
  if (it == map_.end()) {
    return {};
  }
  return bsrvcore::bsrvrun::String(it->second);
}

void RuntimeParameterMap::Set(const bsrvcore::bsrvrun::String& key,
                              const bsrvcore::bsrvrun::String& value) {
  map_[key.ToStdString()] = value.ToStdString();
}

void RuntimeParameterMap::SetRaw(const std::string& key,
                                 const std::string& value) {
  map_[key] = value;
}

}  // namespace bsrvcore::runtime
