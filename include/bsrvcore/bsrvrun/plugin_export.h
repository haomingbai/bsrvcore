/**
 * @file plugin_export.h
 * @brief Export helpers for bsrvrun runtime plugin entry points.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-03
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_BSRVRUN_PLUGIN_EXPORT_H_
#define BSRVCORE_BSRVRUN_PLUGIN_EXPORT_H_

namespace bsrvcore::bsrvrun {

class HttpRequestAspectHandlerFactory;
class HttpRequestHandlerFactory;

}  // namespace bsrvcore::bsrvrun

/**
 * @def BSRVCORE_BSRVRUN_PLUGIN_EXPORT
 * @brief Platform visibility annotation for `bsrvrun` plugin entry points.
 *
 * Apply this to exported C symbols that `bsrvrun` resolves dynamically from a
 * plugin shared library.
 */
#if defined(_WIN32) || defined(__CYGWIN__)
#define BSRVCORE_BSRVRUN_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define BSRVCORE_BSRVRUN_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define BSRVCORE_BSRVRUN_PLUGIN_EXPORT
#endif  // BSRVCORE_BSRVRUN_PLUGIN_EXPORT_H_

/**
 * @def BSRVCORE_BSRVRUN_HANDLER_FACTORY_EXPORT
 * @brief Declares the exported `GetHandlerFactory()` entry point signature.
 *
 * Use this macro directly on the factory function definition in a handler
 * plugin so the symbol is exported with C linkage on every supported platform.
 */
#define BSRVCORE_BSRVRUN_HANDLER_FACTORY_EXPORT \
  extern "C" BSRVCORE_BSRVRUN_PLUGIN_EXPORT     \
      bsrvcore::bsrvrun::HttpRequestHandlerFactory*

/**
 * @def BSRVCORE_BSRVRUN_ASPECT_FACTORY_EXPORT
 * @brief Declares the exported `GetAspectFactory()` entry point signature.
 *
 * Use this macro directly on the factory function definition in an aspect
 * plugin so the symbol is exported with C linkage on every supported platform.
 */
#define BSRVCORE_BSRVRUN_ASPECT_FACTORY_EXPORT \
  extern "C" BSRVCORE_BSRVRUN_PLUGIN_EXPORT    \
      bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory*

#endif
