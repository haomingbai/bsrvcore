/**
 * @file service_provider.h
 * @brief Opaque service slot wrapper used by HttpServer and request tasks.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CORE_SERVICE_PROVIDER_H_
#define BSRVCORE_CORE_SERVICE_PROVIDER_H_

namespace bsrvcore {

/**
 * @brief Opaque non-owning wrapper around one service-provider pointer.
 */
struct ServiceProvider {
  void* pointer{nullptr};

  template <typename T>
  [[nodiscard]] T* Get() const noexcept {
    return static_cast<T*>(pointer);
  }
};

}  // namespace bsrvcore

#endif
