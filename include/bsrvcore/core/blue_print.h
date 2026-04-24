/**
 * @file blue_print.h
 * @brief Mountable route trees for grouping route registrations.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-26
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CORE_BLUE_PRINT_H_
#define BSRVCORE_CORE_BLUE_PRINT_H_

#include <concepts>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/route/cloneable_http_request_aspect_handler.h"
#include "bsrvcore/route/cloneable_http_request_handler.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"

namespace bsrvcore {

class HttpRouteTable;

/**
 * @brief One-shot mountable route tree.
 *
 * A `BluePrint` collects route registrations and can later be mounted under a
 * path prefix on an `HttpServer`. Mounting consumes the blueprint.
 */
class BluePrint : public MovableOnly<BluePrint> {
 public:
  /** @brief Construct an empty one-shot blueprint. */
  BluePrint();
  /** @brief Move-construct a blueprint. */
  BluePrint(BluePrint&&) noexcept;
  /** @brief Move-assign a blueprint. */
  BluePrint& operator=(BluePrint&&) noexcept;
  /** @brief Destroy the blueprint. */
  ~BluePrint();

  /** @brief Add a route entry with an owned handler object. */
  BluePrint* AddRouteEntry(HttpRequestMethod method, std::string_view url,
                           OwnedPtr<HttpRequestHandler> handler);

  /** @brief Add a route entry from a callable. */
  template <typename Func>
    requires requires(std::shared_ptr<HttpServerTask> task, Func fn) {
      { fn(task) };
    }
  BluePrint* AddRouteEntry(HttpRequestMethod method, std::string_view url,
                           Func&& func) {
    using Fn = std::decay_t<Func>;
    return AddRouteEntry(
        method, url,
        AllocateUnique<FunctionRouteHandler<Fn>>(std::forward<Func>(func)));
  }

  /** @brief Add an exclusive route entry with an owned handler object. */
  BluePrint* AddExclusiveRouteEntry(HttpRequestMethod method,
                                    std::string_view url,
                                    OwnedPtr<HttpRequestHandler> handler);

  /** @brief Add an exclusive route entry from a callable. */
  template <typename Func>
    requires requires(std::shared_ptr<HttpServerTask> task, Func fn) {
      { fn(task) };
    }
  BluePrint* AddExclusiveRouteEntry(HttpRequestMethod method,
                                    std::string_view url, Func&& func) {
    using Fn = std::decay_t<Func>;
    return AddExclusiveRouteEntry(
        method, url,
        AllocateUnique<FunctionRouteHandler<Fn>>(std::forward<Func>(func)));
  }

  /** @brief Add a subtree aspect entry with an owned aspect object. */
  BluePrint* AddAspect(HttpRequestMethod method, std::string_view url,
                       OwnedPtr<HttpRequestAspectHandler> aspect);

  /** @brief Add a subtree aspect entry from pre/post callables. */
  template <typename F1, typename F2>
    requires requires(std::shared_ptr<HttpPreServerTask> pre_task,
                      std::shared_ptr<HttpPostServerTask> post_task, F1 fn1,
                      F2 fn2) {
      { fn1(pre_task) };
      { fn2(post_task) };
    }
  BluePrint* AddAspect(HttpRequestMethod method, std::string_view url, F1&& f1,
                       F2&& f2) {
    using PreFn = std::decay_t<F1>;
    using PostFn = std::decay_t<F2>;
    return AddAspect(
        method, url,
        AllocateUnique<FunctionRequestAspectHandler<PreFn, PostFn>>(
            std::forward<F1>(f1), std::forward<F2>(f2)));
  }

  /** @brief Add a terminal aspect entry with an owned aspect object. */
  BluePrint* AddTerminalAspect(HttpRequestMethod method, std::string_view url,
                               OwnedPtr<HttpRequestAspectHandler> aspect);

  /** @brief Add a terminal aspect entry from pre/post callables. */
  template <typename F1, typename F2>
    requires requires(std::shared_ptr<HttpPreServerTask> pre_task,
                      std::shared_ptr<HttpPostServerTask> post_task, F1 fn1,
                      F2 fn2) {
      { fn1(pre_task) };
      { fn2(post_task) };
    }
  BluePrint* AddTerminalAspect(HttpRequestMethod method, std::string_view url,
                               F1&& f1, F2&& f2) {
    using PreFn = std::decay_t<F1>;
    using PostFn = std::decay_t<F2>;
    return AddTerminalAspect(
        method, url,
        AllocateUnique<FunctionRequestAspectHandler<PreFn, PostFn>>(
            std::forward<F1>(f1), std::forward<F2>(f2)));
  }

  /** @brief Override read timeout for one route. */
  BluePrint* SetReadExpiry(HttpRequestMethod method, std::string_view url,
                           std::size_t expiry);
  /** @brief Override write timeout for one route. */
  BluePrint* SetWriteExpiry(HttpRequestMethod method, std::string_view url,
                            std::size_t expiry);
  /** @brief Override maximum body size for one route. */
  BluePrint* SetMaxBodySize(HttpRequestMethod method, std::string_view url,
                            std::size_t size);

 private:
  bool MountInto(std::string_view prefix, HttpRouteTable& route_table) &&;

  class Impl;
  OwnedPtr<Impl> impl_;

  friend class HttpServer;
};

/**
 * @brief Deep-clonable route tree that can be mounted multiple times.
 */
class ReuseableBluePrint : public MovableOnly<ReuseableBluePrint> {
 public:
  /** @brief Construct an empty reusable blueprint. */
  ReuseableBluePrint();
  /** @brief Move-construct a reusable blueprint. */
  ReuseableBluePrint(ReuseableBluePrint&&) noexcept;
  /** @brief Move-assign a reusable blueprint. */
  ReuseableBluePrint& operator=(ReuseableBluePrint&&) noexcept;
  /** @brief Destroy the reusable blueprint. */
  ~ReuseableBluePrint();

  /** @brief Add a route entry with a cloneable handler object. */
  ReuseableBluePrint* AddRouteEntry(
      HttpRequestMethod method, std::string_view url,
      OwnedPtr<CloneableHttpRequestHandler> handler);

  /** @brief Add a route entry from a copyable callable. */
  template <typename Func>
    requires std::copy_constructible<std::decay_t<Func>> &&
             requires(std::shared_ptr<HttpServerTask> task, Func fn) {
               { fn(task) };
             }
  ReuseableBluePrint* AddRouteEntry(HttpRequestMethod method,
                                    std::string_view url, Func&& func) {
    using Fn = std::decay_t<Func>;
    return AddRouteEntry(method, url,
                         AllocateUnique<CloneableFunctionRouteHandler<Fn>>(
                             std::forward<Func>(func)));
  }

  /** @brief Add an exclusive route entry with a cloneable handler object. */
  ReuseableBluePrint* AddExclusiveRouteEntry(
      HttpRequestMethod method, std::string_view url,
      OwnedPtr<CloneableHttpRequestHandler> handler);

  /** @brief Add an exclusive route entry from a copyable callable. */
  template <typename Func>
    requires std::copy_constructible<std::decay_t<Func>> &&
             requires(std::shared_ptr<HttpServerTask> task, Func fn) {
               { fn(task) };
             }
  ReuseableBluePrint* AddExclusiveRouteEntry(HttpRequestMethod method,
                                             std::string_view url,
                                             Func&& func) {
    using Fn = std::decay_t<Func>;
    return AddExclusiveRouteEntry(
        method, url,
        AllocateUnique<CloneableFunctionRouteHandler<Fn>>(
            std::forward<Func>(func)));
  }

  /** @brief Add a subtree aspect entry with a cloneable aspect object. */
  ReuseableBluePrint* AddAspect(
      HttpRequestMethod method, std::string_view url,
      OwnedPtr<CloneableHttpRequestAspectHandler> aspect);

  /** @brief Add a subtree aspect entry from copyable pre/post callables. */
  template <typename F1, typename F2>
    requires std::copy_constructible<std::decay_t<F1>> &&
             std::copy_constructible<std::decay_t<F2>> &&
             requires(std::shared_ptr<HttpPreServerTask> pre_task,
                      std::shared_ptr<HttpPostServerTask> post_task, F1 fn1,
                      F2 fn2) {
               { fn1(pre_task) };
               { fn2(post_task) };
             }
  ReuseableBluePrint* AddAspect(HttpRequestMethod method, std::string_view url,
                                F1&& f1, F2&& f2) {
    using PreFn = std::decay_t<F1>;
    using PostFn = std::decay_t<F2>;
    return AddAspect(
        method, url,
        AllocateUnique<CloneableFunctionRequestAspectHandler<PreFn, PostFn>>(
            std::forward<F1>(f1), std::forward<F2>(f2)));
  }

  /** @brief Add a terminal aspect entry with a cloneable aspect object. */
  ReuseableBluePrint* AddTerminalAspect(
      HttpRequestMethod method, std::string_view url,
      OwnedPtr<CloneableHttpRequestAspectHandler> aspect);

  /** @brief Add a terminal aspect entry from copyable pre/post callables. */
  template <typename F1, typename F2>
    requires std::copy_constructible<std::decay_t<F1>> &&
             std::copy_constructible<std::decay_t<F2>> &&
             requires(std::shared_ptr<HttpPreServerTask> pre_task,
                      std::shared_ptr<HttpPostServerTask> post_task, F1 fn1,
                      F2 fn2) {
               { fn1(pre_task) };
               { fn2(post_task) };
             }
  ReuseableBluePrint* AddTerminalAspect(HttpRequestMethod method,
                                        std::string_view url, F1&& f1,
                                        F2&& f2) {
    using PreFn = std::decay_t<F1>;
    using PostFn = std::decay_t<F2>;
    return AddTerminalAspect(
        method, url,
        AllocateUnique<CloneableFunctionRequestAspectHandler<PreFn, PostFn>>(
            std::forward<F1>(f1), std::forward<F2>(f2)));
  }

  /** @brief Override read timeout for one reusable route entry. */
  ReuseableBluePrint* SetReadExpiry(HttpRequestMethod method,
                                    std::string_view url, std::size_t expiry);
  /** @brief Override write timeout for one reusable route entry. */
  ReuseableBluePrint* SetWriteExpiry(HttpRequestMethod method,
                                     std::string_view url, std::size_t expiry);
  /** @brief Override maximum body size for one reusable route entry. */
  ReuseableBluePrint* SetMaxBodySize(HttpRequestMethod method,
                                     std::string_view url, std::size_t size);

 private:
  bool MountInto(std::string_view prefix, HttpRouteTable& route_table) const;

  class Impl;
  OwnedPtr<Impl> impl_;

  friend class HttpServer;
};

/**
 * @brief Convenience factory for constructing blueprint objects.
 */
class BluePrintFactory : public NonCopyableNonMovable<BluePrintFactory> {
 public:
  /** @brief Create a one-shot blueprint. */
  static BluePrint Create();
  /** @brief Create a reusable blueprint. */
  static ReuseableBluePrint CreateReuseable();
};

}  // namespace bsrvcore

#endif
