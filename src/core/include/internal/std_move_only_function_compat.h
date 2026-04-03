/**
 * @file std_move_only_function_compat.h
 * @brief Compatibility shim for std::move_only_function.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-03
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_INTERNAL_STD_MOVE_ONLY_FUNCTION_COMPAT_H_
#define BSRVCORE_INTERNAL_STD_MOVE_ONLY_FUNCTION_COMPAT_H_

#include <functional>

// Some libc++ variants on macOS runners still do not expose
// std::move_only_function even under C++23. Provide a minimal fallback so
// bthpool can compile consistently.
#if !defined(__cpp_lib_move_only_function) || (__cpp_lib_move_only_function < 202110L)
namespace std {
template <class Signature>
using move_only_function = function<Signature>;
}  // namespace std
#endif

#endif  // BSRVCORE_INTERNAL_STD_MOVE_ONLY_FUNCTION_COMPAT_H_
