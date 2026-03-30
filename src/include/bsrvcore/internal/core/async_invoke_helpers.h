/**
 * @file async_invoke_helpers.h
 * @brief Internal helpers for binding deferred callbacks.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-30
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_CORE_ASYNC_INVOKE_HELPERS_H_
#define BSRVCORE_INTERNAL_CORE_ASYNC_INVOKE_HELPERS_H_

#include <functional>
#include <future>
#include <type_traits>
#include <utility>

#include "bsrvcore/allocator/allocator.h"

namespace bsrvcore::internal::async_invoke {

template <typename Fn, typename... Args>
std::function<void()> BindVoid(Fn&& fn, Args&&... args) {
  auto bound =
      std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
  return [bound = std::move(bound)]() mutable { bound(); };
}

template <typename Starter, typename Fn, typename... Args>
auto StartWithFuture(Starter&& starter, Fn&& fn, Args&&... args)
    -> std::future<std::invoke_result_t<Fn, Args...>> {
  using RT = std::invoke_result_t<Fn, Args...>;

  auto bound =
      std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
  auto task = AllocateShared<std::packaged_task<RT()>>(std::move(bound));
  auto future = task->get_future();

  std::invoke(std::forward<Starter>(starter),
              std::function<void()>{[task = std::move(task)]() { (*task)(); }});
  return future;
}

template <typename Starter, typename Fn, typename... Args>
void StartBound(Starter&& starter, Fn&& fn, Args&&... args) {
  std::invoke(std::forward<Starter>(starter),
              BindVoid(std::forward<Fn>(fn), std::forward<Args>(args)...));
}

}  // namespace bsrvcore::internal::async_invoke

#endif
