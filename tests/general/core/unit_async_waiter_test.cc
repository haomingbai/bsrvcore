#include <gtest/gtest.h>

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/async_waiter.h"

TEST(AsyncWaiterTest, TemplateWaiterExpandsMoveOnlyValues) {
  auto waiter = bsrvcore::AsyncTemplateWaiter<std::unique_ptr<int>,
                                              std::string>::Create();
  std::promise<std::tuple<std::unique_ptr<int>, std::string>> promise;
  auto future = promise.get_future();

  waiter->OnTupleReady(
      [&promise](std::tuple<std::unique_ptr<int>, std::string> values) mutable {
        promise.set_value(std::move(values));
      });

  auto callbacks = waiter->MakeCallbacks();
  std::get<1>(callbacks)("payload");
  std::get<0>(callbacks)(std::make_unique<int>(42));

  auto values = future.get();
  ASSERT_TRUE(std::get<0>(values));
  EXPECT_EQ(*std::get<0>(values), 42);
  EXPECT_EQ(std::get<1>(values), "payload");
}

TEST(AsyncWaiterTest, TemplateWaiterIgnoresDuplicateSlotInvocation) {
  auto waiter = bsrvcore::AsyncTemplateWaiter<int, int>::Create();
  std::promise<std::pair<int, int>> promise;
  auto future = promise.get_future();

  waiter->OnReady([&promise](int first, int second) {
    promise.set_value({first, second});
  });

  auto callbacks = waiter->MakeCallbacks();
  std::get<0>(callbacks)(1);
  std::get<0>(callbacks)(99);
  std::get<1>(callbacks)(2);

  const auto values = future.get();
  EXPECT_EQ(values.first, 1);
  EXPECT_EQ(values.second, 2);
}

TEST(AsyncWaiterTest, SameTypeWaiterSupportsLateRegistration) {
  auto waiter = bsrvcore::AsyncSameTypeWaiter<std::string>::Create(2);
  auto callbacks = waiter->MakeCallbacks();

  callbacks[0]("alpha");
  callbacks[1]("beta");

  std::promise<std::vector<std::string>> promise;
  auto future = promise.get_future();
  waiter->OnReady([&promise](std::vector<std::string> values) mutable {
    promise.set_value(std::move(values));
  });

  const auto values = future.get();
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], "alpha");
  EXPECT_EQ(values[1], "beta");
}

TEST(AsyncWaiterTest, SameTypeWaiterSupportsAllocatedInterfaces) {
  auto waiter = bsrvcore::AsyncSameTypeWaiter<std::string>::Create(2);
  std::promise<bsrvcore::AllocatedVector<std::string>> promise;
  auto future = promise.get_future();
  waiter->OnReadyAllocated(
      [&promise](bsrvcore::AllocatedVector<std::string> values) mutable {
        promise.set_value(std::move(values));
      });

  auto callbacks = waiter->MakeCallbacksAllocated();
  callbacks[0]("left");
  callbacks[1]("right");

  auto values = future.get();
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], "left");
  EXPECT_EQ(values[1], "right");
}

TEST(AsyncWaiterTest, SameTypeVoidWaiterCompletesAfterAllCallbacks) {
  auto waiter = bsrvcore::AsyncSameTypeWaiter<void>::Create(3);
  auto callbacks = waiter->MakeCallbacks();

  std::promise<void> promise;
  auto future = promise.get_future();
  waiter->OnReady([&promise]() { promise.set_value(); });

  callbacks[0]();
  callbacks[0]();
  callbacks[1]();
  callbacks[2]();

  EXPECT_NO_THROW(future.get());
}

TEST(AsyncWaiterTest, SameTypeVoidWaiterSupportsAllocatedCallbacks) {
  auto waiter = bsrvcore::AsyncSameTypeWaiter<void>::Create(2);
  auto callbacks = waiter->MakeCallbacksAllocated();

  std::promise<void> promise;
  auto future = promise.get_future();
  waiter->OnReady([&promise]() { promise.set_value(); });

  callbacks[0]();
  callbacks[1]();

  EXPECT_NO_THROW(future.get());
}

TEST(AsyncWaiterTest, SameTypeWaiterZeroCountCompletesImmediately) {
  auto waiter = bsrvcore::AsyncSameTypeWaiter<int>::Create(0);
  std::promise<std::vector<int>> promise;
  auto future = promise.get_future();

  waiter->OnReady([&promise](std::vector<int> values) mutable {
    promise.set_value(std::move(values));
  });

  EXPECT_TRUE(future.get().empty());
}

TEST(AsyncWaiterTest, CallbacksKeepWaiterAliveAfterOriginalPointerReleased) {
  auto waiter = bsrvcore::AsyncSameTypeWaiter<int>::Create(2);
  auto callbacks = waiter->MakeCallbacks();

  std::promise<std::vector<int>> promise;
  auto future = promise.get_future();
  waiter->OnReady([&promise](std::vector<int> values) mutable {
    promise.set_value(std::move(values));
  });
  waiter.reset();

  callbacks[0](3);
  callbacks[1](7);

  const auto values = future.get();
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], 3);
  EXPECT_EQ(values[1], 7);
}

TEST(AsyncWaiterTest, SameTypeWaiterSupportsManualFulfill) {
  auto waiter = bsrvcore::AsyncSameTypeWaiter<std::string>::Create(2);
  std::promise<std::vector<std::string>> promise;
  auto future = promise.get_future();
  waiter->OnReady([&promise](std::vector<std::string> values) mutable {
    promise.set_value(std::move(values));
  });

  waiter->Fulfill(0, "left");
  waiter->Fulfill(1, "right");

  const auto values = future.get();
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], "left");
  EXPECT_EQ(values[1], "right");
}
