/**
 * @file http_request_method.h
 * @brief
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-24
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 */

#pragma once

#ifndef BSRVCORE_HTTP_REQUEST_METHOD_H
#define BSRVCORE_HTTP_REQUEST_METHOD_H

#include <cstdint>

namespace bsrvcore {
enum class HttpRequestMethod : std::uint8_t {
  kGet = 0,
  kPost,
  kPut,
  kDelete,
  kPatch,
  kHead
};
}  // namespace bsrvcore

#endif
