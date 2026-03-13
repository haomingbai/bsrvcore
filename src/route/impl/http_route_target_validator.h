/**
 * @file http_route_target_validator.h
 * @brief Internal validator for parametric route targets.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_ROUTE_IMPL_HTTP_ROUTE_TARGET_VALIDATOR_H_
#define BSRVCORE_ROUTE_IMPL_HTTP_ROUTE_TARGET_VALIDATOR_H_

#include <string_view>

namespace bsrvcore {

namespace route_internal {

// Validate route target pattern like: "/users/{id}".
//
// Rules (kept consistent with historical behavior):
// - Must start with '/'.
// - Max length 2048.
// - Supports at most one nesting level for parameters: "{name}".
// - Rejects unpaired braces and any ".." in the non-parameter portion.
bool IsValidParametricTarget(std::string_view target);

}  // namespace route_internal

}  // namespace bsrvcore

#endif  // BSRVCORE_ROUTE_IMPL_HTTP_ROUTE_TARGET_VALIDATOR_H_
