/**
 * @file http_request_method.h
 * @brief HTTP request method definitions for RESTful services
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-09-24
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines the HTTP request methods commonly used in RESTful APIs.
 * This is a subset of the full HTTP method specification, focusing
 * on methods relevant to RESTful service implementations.
 */

#pragma once

#ifndef BSRVCORE_HTTP_REQUEST_METHOD_H
#define BSRVCORE_HTTP_REQUEST_METHOD_H

#include <cstdint>

namespace bsrvcore {

/**
 * @brief HTTP request methods for RESTful APIs
 * 
 * Represents the core HTTP methods used in RESTful web services.
 * This enumeration includes only the methods typically needed for
 * REST API implementations, not the complete HTTP method set.
 * 
 * @code
 * // Example usage in route registration
 * route_table->AddRouteEntry(HttpRequestMethod::kGet, "/users", 
 *                           std::make_unique<UserListHandler>());
 * route_table->AddRouteEntry(HttpRequestMethod::kPost, "/users",
 *                           std::make_unique<UserCreateHandler>());
 * route_table->AddRouteEntry(HttpRequestMethod::kPut, "/users/{id}",
 *                           std::make_unique<UserUpdateHandler>());
 * 
 * @endcode
 */
enum class HttpRequestMethod : std::uint8_t {
  kGet = 0,     ///< Retrieve a resource (READ operation)
  kPost,        ///< Create a new resource (CREATE operation)
  kPut,         ///< Update/replace an existing resource (UPDATE operation)
  kDelete,      ///< Remove a resource (DELETE operation)
  kPatch,       ///< Partially update a resource (PARTIAL UPDATE operation)
  kHead         ///< Retrieve resource headers only (no body)
};

}  // namespace bsrvcore

#endif
