/**
 * @file parameter_map_impl.h
 * @brief ParameterMap implementation for bsrvrun runtime.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_PARAMETER_MAP_IMPL_H_
#define BSRVCORE_BSRVRUN_PARAMETER_MAP_IMPL_H_

#include <string>
#include <unordered_map>

#include "bsrvcore/bsrvrun/parameter_map.h"

namespace bsrvcore::runtime {

class RuntimeParameterMap : public bsrvcore::bsrvrun::ParameterMap {
 public:
  RuntimeParameterMap() = default;

  bsrvcore::bsrvrun::String Get(
      const bsrvcore::bsrvrun::String& key) const override;

  void Set(const bsrvcore::bsrvrun::String& key,
           const bsrvcore::bsrvrun::String& value) override;

  void SetRaw(const std::string& key, const std::string& value);

 private:
  std::unordered_map<std::string, std::string> map_;
};

}  // namespace bsrvcore::runtime

#endif
