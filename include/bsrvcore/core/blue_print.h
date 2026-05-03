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
#include <type_traits>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/route/cloneable_http_request_aspect_handler.h"
#include "bsrvcore/route/cloneable_http_request_handler.h"
#include "bsrvcore/route/http_request_aspect_handler.h"
#include "bsrvcore/route/http_request_handler.h"
#include "bsrvcore/route/http_request_method.h"

namespace bsrvcore {

class HttpPostServerTask;
class HttpPreServerTask;
class HttpRouteTable;
class HttpServerTask;

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
  /**
   * @brief Move-assign a blueprint.
   *
   * @return Reference to this blueprint.
   */
  BluePrint& operator=(BluePrint&&) noexcept;
  /** @brief Destroy the blueprint. */
  ~BluePrint();

  /**
   * @brief Add a route entry with an owned handler object.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param handler Handler object to own.
   * @return Pointer to this blueprint for chaining.
   */
  BluePrint* AddRouteEntry(HttpRequestMethod method, std::string_view url,
                           OwnedPtr<HttpRequestHandler> handler);

  /**
   * @brief Add a route entry with `std::make_unique` handler.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param handler Handler object to adopt.
   * @return Pointer to this blueprint for chaining.
   */
  template <typename Handler>
    requires std::derived_from<Handler, HttpRequestHandler>
  BluePrint* AddRouteEntry(HttpRequestMethod method, std::string_view url,
                           std::unique_ptr<Handler> handler) {
    return AddRouteEntry(
        method, url,
        AdoptUniqueAs<HttpRequestHandler, Handler>(std::move(handler)));
  }

  /**
   * @brief Add a route entry from a callable.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param func Callable used to construct a route handler wrapper.
   * @return Pointer to this blueprint for chaining.
   */
  template <typename Func>
    requires requires(const std::shared_ptr<HttpServerTask>& task, Func fn) {
      { fn(task) };
    }
  BluePrint* AddRouteEntry(HttpRequestMethod method, std::string_view url,
                           Func&& func) {
    using Fn = std::decay_t<Func>;
    return AddRouteEntry(
        method, url,
        AllocateUnique<FunctionRouteHandler<Fn>>(std::forward<Func>(func)));
  }

  /**
   * @brief Add an exclusive route entry with an owned handler object.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param handler Handler object to own.
   * @return Pointer to this blueprint for chaining.
   */
  BluePrint* AddExclusiveRouteEntry(HttpRequestMethod method,
                                    std::string_view url,
                                    OwnedPtr<HttpRequestHandler> handler);

  /**
   * @brief Add an exclusive route entry with `std::make_unique` handler.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param handler Handler object to adopt.
   * @return Pointer to this blueprint for chaining.
   */
  template <typename Handler>
    requires std::derived_from<Handler, HttpRequestHandler>
  BluePrint* AddExclusiveRouteEntry(HttpRequestMethod method,
                                    std::string_view url,
                                    std::unique_ptr<Handler> handler) {
    return AddExclusiveRouteEntry(
        method, url,
        AdoptUniqueAs<HttpRequestHandler, Handler>(std::move(handler)));
  }

  /**
   * @brief Add an exclusive route entry from a callable.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param func Callable used to construct a route handler wrapper.
   * @return Pointer to this blueprint for chaining.
   */
  template <typename Func>
    requires requires(const std::shared_ptr<HttpServerTask>& task, Func fn) {
      { fn(task) };
    }
  BluePrint* AddExclusiveRouteEntry(HttpRequestMethod method,
                                    std::string_view url, Func&& func) {
    using Fn = std::decay_t<Func>;
    return AddExclusiveRouteEntry(
        method, url,
        AllocateUnique<FunctionRouteHandler<Fn>>(std::forward<Func>(func)));
  }

  /**
   * @brief Add a subtree aspect entry with an owned aspect object.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route subtree template relative to the blueprint root.
   * @param aspect Aspect object to own.
   * @return Pointer to this blueprint for chaining.
   */
  BluePrint* AddAspect(HttpRequestMethod method, std::string_view url,
                       OwnedPtr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Add a subtree aspect entry with `std::make_unique` aspect.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route subtree template relative to the blueprint root.
   * @param aspect Aspect object to adopt.
   * @return Pointer to this blueprint for chaining.
   */
  template <typename Aspect>
    requires std::derived_from<Aspect, HttpRequestAspectHandler>
  BluePrint* AddAspect(HttpRequestMethod method, std::string_view url,
                       std::unique_ptr<Aspect> aspect) {
    return AddAspect(
        method, url,
        AdoptUniqueAs<HttpRequestAspectHandler, Aspect>(std::move(aspect)));
  }

  /**
   * @brief Add a subtree aspect entry from pre/post callables.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route subtree template relative to the blueprint root.
   * @param f1 Callable invoked during the pre phase.
   * @param f2 Callable invoked during the post phase.
   * @return Pointer to this blueprint for chaining.
   */
  template <typename F1, typename F2>
    requires requires(const std::shared_ptr<HttpPreServerTask>& pre_task,
                      const std::shared_ptr<HttpPostServerTask>& post_task,
                      F1 fn1, F2 fn2) {
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

  /**
   * @brief Add a terminal aspect entry with an owned aspect object.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route template relative to the blueprint root.
   * @param aspect Aspect object to own.
   * @return Pointer to this blueprint for chaining.
   */
  BluePrint* AddTerminalAspect(HttpRequestMethod method, std::string_view url,
                               OwnedPtr<HttpRequestAspectHandler> aspect);

  /**
   * @brief Add a terminal aspect entry with `std::make_unique` aspect.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route template relative to the blueprint root.
   * @param aspect Aspect object to adopt.
   * @return Pointer to this blueprint for chaining.
   */
  template <typename Aspect>
    requires std::derived_from<Aspect, HttpRequestAspectHandler>
  BluePrint* AddTerminalAspect(HttpRequestMethod method, std::string_view url,
                               std::unique_ptr<Aspect> aspect) {
    return AddTerminalAspect(
        method, url,
        AdoptUniqueAs<HttpRequestAspectHandler, Aspect>(std::move(aspect)));
  }

  /**
   * @brief Add a terminal aspect entry from pre/post callables.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route template relative to the blueprint root.
   * @param f1 Callable invoked during the pre phase.
   * @param f2 Callable invoked during the post phase.
   * @return Pointer to this blueprint for chaining.
   */
  template <typename F1, typename F2>
    requires requires(const std::shared_ptr<HttpPreServerTask>& pre_task,
                      const std::shared_ptr<HttpPostServerTask>& post_task,
                      F1 fn1, F2 fn2) {
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

  /**
   * @brief Override read timeout for one route.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param expiry Read timeout in seconds.
   * @return Pointer to this blueprint for chaining.
   */
  BluePrint* SetReadExpiry(HttpRequestMethod method, std::string_view url,
                           std::size_t expiry);
  /**
   * @brief Override write timeout for one route.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param expiry Write timeout in seconds.
   * @return Pointer to this blueprint for chaining.
   */
  BluePrint* SetWriteExpiry(HttpRequestMethod method, std::string_view url,
                            std::size_t expiry);
  /**
   * @brief Override maximum body size for one route.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param size Maximum request body size in bytes.
   * @return Pointer to this blueprint for chaining.
   */
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
  /**
   * @brief Move-assign a reusable blueprint.
   *
   * @return Reference to this reusable blueprint.
   */
  ReuseableBluePrint& operator=(ReuseableBluePrint&&) noexcept;
  /** @brief Destroy the reusable blueprint. */
  ~ReuseableBluePrint();

  /**
   * @brief Add a route entry with a cloneable handler object.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param handler Cloneable handler object to own.
   * @return Pointer to this reusable blueprint for chaining.
   */
  ReuseableBluePrint* AddRouteEntry(
      HttpRequestMethod method, std::string_view url,
      OwnedPtr<CloneableHttpRequestHandler> handler);

  /**
   * @brief Add a route entry with `std::make_unique` cloneable handler.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param handler Cloneable handler object to adopt.
   * @return Pointer to this reusable blueprint for chaining.
   */
  template <typename Handler>
    requires std::derived_from<Handler, CloneableHttpRequestHandler>
  ReuseableBluePrint* AddRouteEntry(HttpRequestMethod method,
                                    std::string_view url,
                                    std::unique_ptr<Handler> handler) {
    return AddRouteEntry(method, url,
                         AdoptUniqueAs<CloneableHttpRequestHandler, Handler>(
                             std::move(handler)));
  }

  /**
   * @brief Add a route entry from a copyable callable.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param func Copyable callable used to construct a route handler wrapper.
   * @return Pointer to this reusable blueprint for chaining.
   */
  template <typename Func>
    requires std::copy_constructible<std::decay_t<Func>> &&
             requires(const std::shared_ptr<HttpServerTask>& task, Func fn) {
               { fn(task) };
             }
  ReuseableBluePrint* AddRouteEntry(HttpRequestMethod method,
                                    std::string_view url, Func&& func) {
    using Fn = std::decay_t<Func>;
    return AddRouteEntry(method, url,
                         AllocateUnique<CloneableFunctionRouteHandler<Fn>>(
                             std::forward<Func>(func)));
  }

  /**
   * @brief Add an exclusive route entry with a cloneable handler object.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param handler Cloneable handler object to own.
   * @return Pointer to this reusable blueprint for chaining.
   */
  ReuseableBluePrint* AddExclusiveRouteEntry(
      HttpRequestMethod method, std::string_view url,
      OwnedPtr<CloneableHttpRequestHandler> handler);

  /**
   * @brief Add an exclusive route entry with `std::make_unique` cloneable
   * handler.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param handler Cloneable handler object to adopt.
   * @return Pointer to this reusable blueprint for chaining.
   */
  template <typename Handler>
    requires std::derived_from<Handler, CloneableHttpRequestHandler>
  ReuseableBluePrint* AddExclusiveRouteEntry(HttpRequestMethod method,
                                             std::string_view url,
                                             std::unique_ptr<Handler> handler) {
    return AddExclusiveRouteEntry(
        method, url,
        AdoptUniqueAs<CloneableHttpRequestHandler, Handler>(
            std::move(handler)));
  }

  /**
   * @brief Add an exclusive route entry from a copyable callable.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param func Copyable callable used to construct a route handler wrapper.
   * @return Pointer to this reusable blueprint for chaining.
   */
  template <typename Func>
    requires std::copy_constructible<std::decay_t<Func>> &&
             requires(const std::shared_ptr<HttpServerTask>& task, Func fn) {
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

  /**
   * @brief Add a subtree aspect entry with a cloneable aspect object.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route subtree template relative to the blueprint root.
   * @param aspect Cloneable aspect object to own.
   * @return Pointer to this reusable blueprint for chaining.
   */
  ReuseableBluePrint* AddAspect(
      HttpRequestMethod method, std::string_view url,
      OwnedPtr<CloneableHttpRequestAspectHandler> aspect);

  /**
   * @brief Add a subtree aspect entry with `std::make_unique` cloneable
   * aspect.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route subtree template relative to the blueprint root.
   * @param aspect Cloneable aspect object to adopt.
   * @return Pointer to this reusable blueprint for chaining.
   */
  template <typename Aspect>
    requires std::derived_from<Aspect, CloneableHttpRequestAspectHandler>
  ReuseableBluePrint* AddAspect(HttpRequestMethod method, std::string_view url,
                                std::unique_ptr<Aspect> aspect) {
    return AddAspect(method, url,
                     AdoptUniqueAs<CloneableHttpRequestAspectHandler, Aspect>(
                         std::move(aspect)));
  }

  /**
   * @brief Add a subtree aspect entry from copyable pre/post callables.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route subtree template relative to the blueprint root.
   * @param f1 Copyable callable invoked during the pre phase.
   * @param f2 Copyable callable invoked during the post phase.
   * @return Pointer to this reusable blueprint for chaining.
   */
  template <typename F1, typename F2>
    requires std::copy_constructible<std::decay_t<F1>> &&
             std::copy_constructible<std::decay_t<F2>> &&
             requires(const std::shared_ptr<HttpPreServerTask>& pre_task,
                      const std::shared_ptr<HttpPostServerTask>& post_task,
                      F1 fn1, F2 fn2) {
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

  /**
   * @brief Add a terminal aspect entry with a cloneable aspect object.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route template relative to the blueprint root.
   * @param aspect Cloneable aspect object to own.
   * @return Pointer to this reusable blueprint for chaining.
   */
  ReuseableBluePrint* AddTerminalAspect(
      HttpRequestMethod method, std::string_view url,
      OwnedPtr<CloneableHttpRequestAspectHandler> aspect);

  /**
   * @brief Add a terminal aspect entry with `std::make_unique` cloneable
   * aspect.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route template relative to the blueprint root.
   * @param aspect Cloneable aspect object to adopt.
   * @return Pointer to this reusable blueprint for chaining.
   */
  template <typename Aspect>
    requires std::derived_from<Aspect, CloneableHttpRequestAspectHandler>
  ReuseableBluePrint* AddTerminalAspect(HttpRequestMethod method,
                                        std::string_view url,
                                        std::unique_ptr<Aspect> aspect) {
    return AddTerminalAspect(
        method, url,
        AdoptUniqueAs<CloneableHttpRequestAspectHandler, Aspect>(
            std::move(aspect)));
  }

  /**
   * @brief Add a terminal aspect entry from copyable pre/post callables.
   *
   * @param method HTTP method bucket for the aspect.
   * @param url Route template relative to the blueprint root.
   * @param f1 Copyable callable invoked during the pre phase.
   * @param f2 Copyable callable invoked during the post phase.
   * @return Pointer to this reusable blueprint for chaining.
   */
  template <typename F1, typename F2>
    requires std::copy_constructible<std::decay_t<F1>> &&
             std::copy_constructible<std::decay_t<F2>> &&
             requires(const std::shared_ptr<HttpPreServerTask>& pre_task,
                      const std::shared_ptr<HttpPostServerTask>& post_task,
                      F1 fn1, F2 fn2) {
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

  /**
   * @brief Override read timeout for one reusable route entry.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param expiry Read timeout in seconds.
   * @return Pointer to this reusable blueprint for chaining.
   */
  ReuseableBluePrint* SetReadExpiry(HttpRequestMethod method,
                                    std::string_view url, std::size_t expiry);
  /**
   * @brief Override write timeout for one reusable route entry.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param expiry Write timeout in seconds.
   * @return Pointer to this reusable blueprint for chaining.
   */
  ReuseableBluePrint* SetWriteExpiry(HttpRequestMethod method,
                                     std::string_view url, std::size_t expiry);
  /**
   * @brief Override maximum body size for one reusable route entry.
   *
   * @param method HTTP method bucket for the route.
   * @param url Route template relative to the blueprint root.
   * @param size Maximum request body size in bytes.
   * @return Pointer to this reusable blueprint for chaining.
   */
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
  /**
   * @brief Create a one-shot blueprint.
   *
   * @return Empty one-shot blueprint.
   */
  static BluePrint Create();
  /**
   * @brief Create a reusable blueprint.
   *
   * @return Empty reusable blueprint.
   */
  static ReuseableBluePrint CreateReuseable();
};

}  // namespace bsrvcore

#endif
