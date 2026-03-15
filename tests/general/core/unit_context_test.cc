#include <gtest/gtest.h>

#include <barrier>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "bsrvcore/attribute.h"
#include "bsrvcore/context.h"

namespace {

// Simple attribute used to verify Context stores shared polymorphic values.
class IntAttribute : public bsrvcore::CloneableAttribute<IntAttribute> {
 public:
  explicit IntAttribute(int v) : value(v) {}

  bool Equals(const bsrvcore::Attribute& other) const noexcept override {
    auto* ptr = dynamic_cast<const IntAttribute*>(&other);
    return ptr != nullptr && ptr->value == value;
  }

  int value;
};

}  // namespace

// Verify basic Set/Get/Has behavior of Context.
TEST(ContextTest, SetGetHas) {
  bsrvcore::Context ctx;

  EXPECT_FALSE(ctx.HasAttribute("k1"));

  auto attr = std::make_shared<IntAttribute>(42);
  ctx.SetAttribute("k1", attr);

  EXPECT_TRUE(ctx.HasAttribute("k1"));
  auto got = ctx.GetAttribute("k1");
  ASSERT_NE(got, nullptr);
  EXPECT_TRUE(attr->Equals(*got));
}

// Verify Context remains safe under concurrent Set/Get calls.
TEST(ContextTest, ConcurrentSetGet) {
  bsrvcore::Context ctx;

  constexpr int kThreads = 8;
  constexpr int kKeys = 64;
  constexpr int kIterations = 5000;

  for (int i = 0; i < kKeys; ++i) {
    ctx.SetAttribute("k" + std::to_string(i),
                     std::make_shared<IntAttribute>(i));
  }

  std::barrier sync{kThreads};
  std::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&ctx, &sync, t] {
      sync.arrive_and_wait();
      for (int i = 0; i < kIterations; ++i) {
        int idx = (t + i) % kKeys;
        auto key = "k" + std::to_string(idx);
        ctx.SetAttribute(key, std::make_shared<IntAttribute>(idx + 1));
        auto got = ctx.GetAttribute(key);
        ASSERT_NE(got, nullptr);
      }
    });
  }

  for (auto& th : workers) {
    th.join();
  }

  for (int i = 0; i < kKeys; ++i) {
    auto key = "k" + std::to_string(i);
    EXPECT_TRUE(ctx.HasAttribute(key));
    EXPECT_NE(ctx.GetAttribute(key), nullptr);
  }
}
