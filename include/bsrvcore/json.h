/**
 * @file json.h
 * @brief Public Boost.JSON aliases used by bsrvcore APIs.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-02
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_JSON_H_
#define BSRVCORE_JSON_H_

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

namespace bsrvcore {

/** @brief Namespace alias to the underlying Boost.JSON implementation. */
namespace json = boost::json;

/** @brief Public alias of the generic JSON value type. */
using JsonValue = json::value;
/** @brief Public alias of the JSON object type. */
using JsonObject = json::object;
/** @brief Public alias of the JSON array type. */
using JsonArray = json::array;
/** @brief Public alias of the JSON string type. */
using JsonString = json::string;
/** @brief Public alias of the JSON parsing error code type. */
using JsonErrorCode = boost::system::error_code;

}  // namespace bsrvcore

#endif  // BSRVCORE_JSON_H_
