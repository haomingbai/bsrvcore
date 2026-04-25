/**
 * @file async_waiter.h
 * @brief Header-only helpers for converging parallel async callbacks.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CORE_ASYNC_WAITER_H_
#define BSRVCORE_CORE_ASYNC_WAITER_H_

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/trait.h"

namespace bsrvcore {

namespace async_waiter_detail {
/// @cond INTERNAL

template <typename Waiter, std::size_t Index, typename Value>
class TypedSlotCallback {
 public:
  TypedSlotCallback() = default;

  explicit TypedSlotCallback(std::shared_ptr<Waiter> waiter)
      : waiter_(std::move(waiter)) {}

  void operator()(Value value) const {
    if (waiter_) {
      waiter_->template Fulfill<Index>(std::move(value));
    }
  }

 private:
  std::shared_ptr<Waiter> waiter_;
};

template <typename Waiter, typename Value>
class IndexedCallback {
 public:
  IndexedCallback() = default;

  IndexedCallback(std::shared_ptr<Waiter> waiter, std::size_t index)
      : waiter_(std::move(waiter)), index_(index) {}

  void operator()(Value value) const {
    if (waiter_) {
      waiter_->Fulfill(index_, std::move(value));
    }
  }

 private:
  std::shared_ptr<Waiter> waiter_;
  std::size_t index_{0};
};

template <typename Waiter>
class VoidIndexedCallback {
 public:
  VoidIndexedCallback() = default;

  VoidIndexedCallback(std::shared_ptr<Waiter> waiter, std::size_t index)
      : waiter_(std::move(waiter)), index_(index) {}

  void operator()() const {
    if (waiter_) {
      waiter_->Fulfill(index_);
    }
  }

 private:
  std::shared_ptr<Waiter> waiter_;
  std::size_t index_{0};
};

template <typename... Ts>
class ExpandedReadyCallback {
 public:
  ExpandedReadyCallback() = default;

  explicit ExpandedReadyCallback(std::function<void(Ts...)> callback)
      : callback_(std::move(callback)) {}

  void operator()(std::tuple<Ts...> values) const {
    if (callback_) {
      std::apply(callback_, std::move(values));
    }
  }

 private:
  std::function<void(Ts...)> callback_;
};

}  // namespace async_waiter_detail
/// @endcond

template <typename... Ts>
/**
 * @brief Wait for one value of each type in `Ts...` before firing a callback.
 *
 * @tparam Ts Value types collected by the waiter.
 */
class AsyncTemplateWaiter
    : public std::enable_shared_from_this<AsyncTemplateWaiter<Ts...>>,
      public NonCopyableNonMovable<AsyncTemplateWaiter<Ts...>> {
  static_assert(sizeof...(Ts) > 0,
                "AsyncTemplateWaiter requires at least one value type");

 public:
  /** @brief Tuple type produced when all slots are fulfilled. */
  using TupleType = std::tuple<Ts...>;
  /** @brief Callback type receiving the collected tuple. */
  using TupleCallback = std::function<void(TupleType)>;
  /** @brief Callback type receiving the tuple expanded as parameters. */
  using ExpandedCallback = std::function<void(Ts...)>;

  /** @brief Create a shared waiter instance. */
  [[nodiscard]] static std::shared_ptr<AsyncTemplateWaiter> Create() {
    struct SharedEnabler final : AsyncTemplateWaiter {
      SharedEnabler() : AsyncTemplateWaiter(PrivateTag{}) {}
    };
    return AllocateShared<SharedEnabler>();
  }

  /** @brief Register the final callback that receives a tuple. */
  void OnTupleReady(TupleCallback callback) {
    ReadyCompletion completion;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (fired_) {
        return;
      }

      tuple_callback_ = std::move(callback);
      completion = TakeCompletionLocked();
    }

    RunCompletion(std::move(completion));
  }

  /** @brief Register the final callback with expanded parameters. */
  void OnReady(ExpandedCallback callback) {
    OnTupleReady(
        async_waiter_detail::ExpandedReadyCallback<Ts...>(std::move(callback)));
  }

  /** @brief Fulfill one compile-time-selected slot. */
  template <std::size_t Index>
  void Fulfill(std::tuple_element_t<Index, TupleType> value) {
    ReadyCompletion completion;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (fired_ || slot_ready_[Index]) {
        return;
      }

      std::get<Index>(values_).emplace(std::move(value));
      slot_ready_[Index] = true;
      if (remaining_ != 0) {
        --remaining_;
      }
      completion = TakeCompletionLocked();
    }

    RunCompletion(std::move(completion));
  }

  /** @brief Create one callback per tuple slot for fan-out async flows. */
  [[nodiscard]] auto MakeCallbacks() {
    return MakeCallbacksImpl(std::index_sequence_for<Ts...>{});
  }

 private:
  struct PrivateTag {};
  using OptionalTupleType = std::tuple<std::optional<Ts>...>;
  using ReadyCompletion = std::optional<std::pair<TupleCallback, TupleType>>;

  explicit AsyncTemplateWaiter(PrivateTag) {}

  template <std::size_t... Indices>
  [[nodiscard]] auto MakeCallbacksImpl(std::index_sequence<Indices...>) {
    auto self = this->shared_from_this();
    return std::make_tuple(std::function<void(Ts)>{
        async_waiter_detail::TypedSlotCallback<AsyncTemplateWaiter, Indices,
                                               Ts>(self)}...);
  }

  template <std::size_t... Indices>
  [[nodiscard]] TupleType MoveTupleLocked(std::index_sequence<Indices...>) {
    return TupleType{std::move(*std::get<Indices>(values_))...};
  }

  [[nodiscard]] ReadyCompletion TakeCompletionLocked() {
    if (fired_ || (remaining_ != 0) || !tuple_callback_) {
      return std::nullopt;
    }

    fired_ = true;
    return std::make_optional(
        std::make_pair(std::move(tuple_callback_),
                       MoveTupleLocked(std::index_sequence_for<Ts...>{})));
  }

  static void RunCompletion(ReadyCompletion completion) {
    if (completion.has_value()) {
      completion->first(std::move(completion->second));
    }
  }

  std::mutex mutex_;
  bool fired_{false};
  std::size_t remaining_{sizeof...(Ts)};
  std::array<bool, sizeof...(Ts)> slot_ready_{};
  OptionalTupleType values_{};
  TupleCallback tuple_callback_;
};

template <typename T>
/**
 * @brief Wait for `N` values of the same type before firing a callback.
 *
 * @tparam T Value type collected by the waiter.
 */
class AsyncSameTypeWaiter
    : public std::enable_shared_from_this<AsyncSameTypeWaiter<T>>,
      public NonCopyableNonMovable<AsyncSameTypeWaiter<T>> {
 public:
  /** @brief Backward-compatible collected value list (`std::vector`). */
  using CompatValueList = std::vector<T>;
  /** @brief Allocator-backed collected value list. */
  using AllocatedValueList = AllocatedVector<T>;
  /** @brief Backward-compatible callback receiving slot-ordered values. */
  using CompatCallback = std::function<void(CompatValueList)>;
  /** @brief Callback type receiving all collected values in slot order. */
  using Callback = CompatCallback;
  /** @brief Allocator-backed callback type for hot-path callers. */
  using AllocatedCallback = std::function<void(AllocatedValueList)>;
  /** @brief Backward-compatible callback list type. */
  using CompatFulfillCallbacks = std::vector<std::function<void(T)>>;
  /** @brief Allocator-backed callback list type. */
  using AllocatedFulfillCallbacks = AllocatedVector<std::function<void(T)>>;

  /** @brief Create a shared waiter instance. */
  [[nodiscard]] static std::shared_ptr<AsyncSameTypeWaiter> Create(
      std::size_t wait_count) {
    struct SharedEnabler final : AsyncSameTypeWaiter {
      explicit SharedEnabler(std::size_t count)
          : AsyncSameTypeWaiter(PrivateTag{}, count) {}
    };
    return AllocateShared<SharedEnabler>(wait_count);
  }

  /** @brief Register the final callback for the collected values. */
  void OnReady(Callback callback) {
    if (!callback) {
      OnReadyAllocated(AllocatedCallback{});
      return;
    }
    OnReadyAllocated([callback = std::move(callback)](
                         AllocatedVector<T> values) mutable {
      callback(detail::ToStdVector(std::move(values)));
    });
  }

  /** @brief Register allocator-backed callback to avoid compatibility copy. */
  void OnReadyAllocated(AllocatedCallback callback) {
    ReadyCompletion completion;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (fired_) {
        return;
      }

      callback_ = std::move(callback);
      completion = TakeCompletionLocked();
    }

    RunCompletion(std::move(completion));
  }

  /** @brief Fulfill one runtime-selected slot. */
  void Fulfill(std::size_t index, T value) {
    ReadyCompletion completion;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (fired_ || (index >= slot_ready_.size()) || slot_ready_[index]) {
        return;
      }

      values_[index].emplace(std::move(value));
      slot_ready_[index] = true;
      if (remaining_ != 0) {
        --remaining_;
      }
      completion = TakeCompletionLocked();
    }

    RunCompletion(std::move(completion));
  }

  /** @brief Create one callback per runtime slot for fan-out async flows. */
  [[nodiscard]] CompatFulfillCallbacks MakeCallbacks() {
    return detail::ToStdVector(MakeCallbacksAllocated());
  }

  /**
   * @brief Create allocator-backed callbacks for runtime slots.
   *
   * Avoids compatibility conversion for high-throughput flows.
   */
  [[nodiscard]] AllocatedFulfillCallbacks MakeCallbacksAllocated() {
    auto self = this->shared_from_this();
    AllocatedFulfillCallbacks callbacks;
    callbacks.reserve(slot_ready_.size());
    for (std::size_t i = 0; i < slot_ready_.size(); ++i) {
      callbacks.push_back(std::function<void(T)>{
          async_waiter_detail::IndexedCallback<AsyncSameTypeWaiter, T>(self,
                                                                       i)});
    }
    return callbacks;
  }

 private:
  struct PrivateTag {};
  using ReadyCompletion =
      std::optional<std::pair<AllocatedCallback, AllocatedValueList>>;

  explicit AsyncSameTypeWaiter(PrivateTag, std::size_t wait_count)
      : remaining_(wait_count),
        slot_ready_(wait_count, false),
        values_(wait_count) {}

  [[nodiscard]] ReadyCompletion TakeCompletionLocked() {
    if (fired_ || (remaining_ != 0) || !callback_) {
      return std::nullopt;
    }

    fired_ = true;
    AllocatedValueList ready_values;
    ready_values.reserve(values_.size());
    for (auto& item : values_) {
      ready_values.push_back(std::move(*item));
    }
    return std::make_optional(
        std::make_pair(std::move(callback_), std::move(ready_values)));
  }

  static void RunCompletion(ReadyCompletion completion) {
    if (completion.has_value()) {
      completion->first(std::move(completion->second));
    }
  }

  std::mutex mutex_;
  bool fired_{false};
  std::size_t remaining_{0};
  AllocatedVector<bool> slot_ready_;
  AllocatedVector<std::optional<T>> values_;
  AllocatedCallback callback_;
};

template <>
/**
 * @brief Wait for `N` completion signals when no per-slot value is needed.
 */
class AsyncSameTypeWaiter<void>
    : public std::enable_shared_from_this<AsyncSameTypeWaiter<void>>,
      public NonCopyableNonMovable<AsyncSameTypeWaiter<void>> {
 public:
  /** @brief Callback type invoked when all slots complete. */
  using Callback = std::function<void()>;
  /** @brief Backward-compatible callback list type. */
  using CompatFulfillCallbacks = std::vector<std::function<void()>>;
  /** @brief Allocator-backed callback list type. */
  using AllocatedFulfillCallbacks = AllocatedVector<std::function<void()>>;

  /** @brief Create a shared waiter instance. */
  [[nodiscard]] static std::shared_ptr<AsyncSameTypeWaiter> Create(
      std::size_t wait_count) {
    struct SharedEnabler final : AsyncSameTypeWaiter {
      explicit SharedEnabler(std::size_t count)
          : AsyncSameTypeWaiter(PrivateTag{}, count) {}
    };
    return AllocateShared<SharedEnabler>(wait_count);
  }

  /** @brief Register the final callback for the completion barrier. */
  void OnReady(Callback callback) {
    ReadyCompletion completion;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (fired_) {
        return;
      }

      callback_ = std::move(callback);
      completion = TakeCompletionLocked();
    }

    RunCompletion(std::move(completion));
  }

  /** @brief Fulfill one runtime-selected slot. */
  void Fulfill(std::size_t index) {
    ReadyCompletion completion;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (fired_ || (index >= slot_ready_.size()) || slot_ready_[index]) {
        return;
      }

      slot_ready_[index] = true;
      if (remaining_ != 0) {
        --remaining_;
      }
      completion = TakeCompletionLocked();
    }

    RunCompletion(std::move(completion));
  }

  /** @brief Create one callback per runtime slot for fan-out async flows. */
  [[nodiscard]] CompatFulfillCallbacks MakeCallbacks() {
    return detail::ToStdVector(MakeCallbacksAllocated());
  }

  /**
   * @brief Create allocator-backed callbacks for runtime slots.
   *
   * Avoids compatibility conversion for high-throughput flows.
   */
  [[nodiscard]] AllocatedFulfillCallbacks MakeCallbacksAllocated() {
    auto self = this->shared_from_this();
    AllocatedFulfillCallbacks callbacks;
    callbacks.reserve(slot_ready_.size());
    for (std::size_t i = 0; i < slot_ready_.size(); ++i) {
      callbacks.push_back(std::function<void()>{
          async_waiter_detail::VoidIndexedCallback<AsyncSameTypeWaiter>(self,
                                                                        i)});
    }
    return callbacks;
  }

 private:
  struct PrivateTag {};
  using ReadyCompletion = std::optional<Callback>;

  explicit AsyncSameTypeWaiter(PrivateTag, std::size_t wait_count)
      : remaining_(wait_count), slot_ready_(wait_count, false) {}

  [[nodiscard]] ReadyCompletion TakeCompletionLocked() {
    if (fired_ || (remaining_ != 0) || !callback_) {
      return std::nullopt;
    }

    fired_ = true;
    return std::move(callback_);
  }

  static void RunCompletion(ReadyCompletion completion) {
    if (completion.has_value()) {
      (*completion)();
    }
  }

  std::mutex mutex_;
  bool fired_{false};
  std::size_t remaining_{0};
  AllocatedVector<bool> slot_ready_;
  Callback callback_;
};

}  // namespace bsrvcore

#endif  // BSRVCORE_CORE_ASYNC_WAITER_H_
