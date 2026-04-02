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

namespace bsrvcore {

namespace json = boost::json;

using JsonValue = json::value;
using JsonObject = json::object;
using JsonArray = json::array;
using JsonString = json::string;
using JsonErrorCode = json::error_code;

}  // namespace bsrvcore

#endif  // BSRVCORE_JSON_H_
