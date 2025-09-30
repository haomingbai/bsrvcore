/**
 * @file attribute.h
 * @brief Polymorphic attribute system with cloning and type management
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-25
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides a polymorphic attribute base class with cloning support,
 * type identification, and value semantics. Used for storing typed
 * data in generic containers while preserving runtime type information.
 */

#pragma once

#ifndef BSRVCORE_ATTRIBUTE_H_
#define BSRVCORE_ATTRIBUTE_H_

#include <cstddef>
#include <memory>
#include <string>
#include <typeindex>

namespace bsrvcore {

/**
 * @brief Base class for polymorphic attributes with value semantics
 * 
 * Attribute provides a common interface for storing typed data in
 * generic containers while supporting cloning, type identification,
 * and equality comparison. Useful for context data, configuration,
 * and extensible metadata systems.
 * 
 * @code
 * // Example custom attribute
 * class UserAttribute : public CloneableAttribute<UserAttribute> {
 * public:
 *   std::string name;
 *   int level;
 *   
 *   std::string ToString() const override { 
 *     return "User(" + name + ", " + std::to_string(level) + ")"; 
 *   }
 *   
 *   bool Equals(const Attribute& other) const override {
 *     if (auto* user = dynamic_cast<const UserAttribute*>(&other)) {
 *       return name == user->name && level == user->level;
 *     }
 *     return false;
 *   }
 * };
 * 
 * // Usage in container
 * std::unique_ptr<Attribute> attr = std::make_unique<UserAttribute>();
 * auto cloned = attr->Clone();  // Deep copy
 * @endcode
 */
class Attribute {
 public:
  /**
   * @brief Create a deep copy of this attribute
   * @return Unique pointer to the cloned attribute
   * 
   * @note Pure virtual - must be implemented by derived classes
   */
  virtual std::unique_ptr<Attribute> Clone() const = 0;

  /**
   * @brief Convert attribute to string representation
   * @return String representation of the attribute
   * 
   * @note Default implementation returns type name
   */
  virtual std::string ToString() const { return Type().name(); }

  /**
   * @brief Compare attributes for equality
   * @param another Attribute to compare with
   * @return true if attributes are equal
   * 
   * @note Default implementation compares by address
   */
  virtual bool Equals(const Attribute &another) const noexcept {
    return this == &another;
  };

  /**
   * @brief Get the type information for this attribute
   * @return Type index identifying the concrete attribute type
   * 
   * @note Default implementation returns typeid(*this)
   */
  virtual std::type_index Type() const noexcept { return typeid(*this); }

  /**
   * @brief Compute hash value for this attribute
   * @return Hash value suitable for use in hash-based containers
   * 
   * @note Default implementation hashes the object address
   */
  virtual std::size_t Hash() const noexcept {
    return std::hash<std::uintptr_t>()(reinterpret_cast<std::uintptr_t>(this));
  };

  /**
   * @brief Virtual destructor for proper cleanup
   */
  virtual ~Attribute() = default;
};

/**
 * @brief CRTP template for easily creating cloneable attributes
 * 
 * Provides automatic implementation of the Clone() method using
 * the Curiously Recurring Template Pattern (CRTP). Derived classes
 * inherit cloning functionality without manual implementation.
 * 
 * @tparam Derived The concrete attribute class being implemented
 * 
 * @code
 * // Simple cloneable attribute example
 * class MyAttribute : public CloneableAttribute<MyAttribute> {
 *   // No need to implement Clone() - it's automatically provided
 *   std::string data;
 * };
 * 
 * auto original = std::make_unique<MyAttribute>();
 * auto copy = original->Clone();  // Returns std::unique_ptr<Attribute>
 * @endcode
 */
template <typename Derived>
struct CloneableAttribute : Attribute {
  /**
   * @brief Automatically implemented clone method
   * @return Unique pointer to a copy of the derived attribute
   */
  std::unique_ptr<Attribute> Clone() const override {
    return std::make_unique<Derived>(static_cast<const Derived &>(*this));
  }
};

}  // namespace bsrvcore

#endif
