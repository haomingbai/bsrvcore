/**
 * @file attribute.h
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

#ifndef BSRVCORE_ATTRIBUTE_H_
#define BSRVCORE_ATTRIBUTE_H_

#include <cstddef>
#include <memory>
#include <string>
#include <typeindex>

namespace bsrvcore {
class Attribute {
 public:
  virtual std::unique_ptr<Attribute> Clone() const = 0;

  virtual std::string ToString() const { return Type().name(); }

  virtual bool Equals(const Attribute &another) const noexcept {
    return this == &another;
  };

  virtual std::type_index Type() const noexcept { return typeid(*this); }

  virtual std::size_t Hash() const noexcept {
    return std::hash<std::uintptr_t>()(reinterpret_cast<std::uintptr_t>(this));
  };

  virtual ~Attribute() = default;
};

template <typename Derived>
struct CloneableAttribute : Attribute {
  std::unique_ptr<Attribute> Clone() const override {
    return std::make_unique<Derived>(static_cast<const Derived &>(*this));
  }
};
}  // namespace bsrvcore

#endif
