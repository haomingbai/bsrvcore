#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "bsrvcore/attribute.h"

namespace {

// Example attribute to validate Clone/Type/Equals behaviors.
class UserAttribute : public bsrvcore::CloneableAttribute<UserAttribute> {
 public:
  UserAttribute(std::string name, int level)
      : name_(std::move(name)), level_(level) {}

  bool Equals(const bsrvcore::Attribute& other) const noexcept override {
    auto* ptr = dynamic_cast<const UserAttribute*>(&other);
    return ptr != nullptr && ptr->name_ == name_ && ptr->level_ == level_;
  }

  std::string name_;
  int level_;
};

}  // namespace

// Verify Clone creates a deep copy and preserves type/equality semantics.
TEST(AttributeTest, CloneAndEquals) {
  UserAttribute original("alice", 7);
  auto cloned = original.Clone();

  ASSERT_NE(cloned, nullptr);
  EXPECT_NE(cloned.get(), &original);
  EXPECT_TRUE(original.Equals(*cloned));
  EXPECT_EQ(original.Type(), cloned->Type());
}
