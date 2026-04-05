/**
 * @file http_server_util.cc
 * @brief HttpServer utility helpers.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2025-10-03
 *
 * Copyright © 2025 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements helper conversions and small utilities used by HttpServer.
 */

#include <boost/beast/http/verb.hpp>

#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"

using namespace bsrvcore;

HttpRequestMethod HttpServer::BeastHttpVerbToHttpRequestMethod(HttpVerb verb) {
  switch (verb) {
    case HttpVerb::get:
      return HttpRequestMethod::kGet;
    case HttpVerb::post:
      return HttpRequestMethod::kPost;
    case HttpVerb::put:
      return HttpRequestMethod::kPut;
    case HttpVerb::delete_:
      return HttpRequestMethod::kDelete;
    case HttpVerb::patch:
      return HttpRequestMethod::kPatch;
    case HttpVerb::head:
      return HttpRequestMethod::kHead;
    default:
      return HttpRequestMethod::kGet;
  }
}

HttpVerb HttpServer::HttpRequestMethodToBeastHttpVerb(
    HttpRequestMethod method) {
  switch (method) {
    case HttpRequestMethod::kGet:
      return HttpVerb::get;
    case HttpRequestMethod::kPost:
      return HttpVerb::post;
    case HttpRequestMethod::kPut:
      return HttpVerb::put;
    case HttpRequestMethod::kDelete:
      return HttpVerb::delete_;
    case HttpRequestMethod::kPatch:
      return HttpVerb::patch;
    case HttpRequestMethod::kHead:
      return HttpVerb::head;
  }
  return HttpVerb::get;
}
