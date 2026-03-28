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

HttpRequestMethod HttpServer::BeastHttpVerbToHttpRequestMethod(
    boost::beast::http::verb verb) {
  switch (verb) {
    case boost::beast::http::verb::get:
      return HttpRequestMethod::kGet;
    case boost::beast::http::verb::post:
      return HttpRequestMethod::kPost;
    case boost::beast::http::verb::put:
      return HttpRequestMethod::kPut;
    case boost::beast::http::verb::delete_:
      return HttpRequestMethod::kDelete;
    case boost::beast::http::verb::patch:
      return HttpRequestMethod::kPatch;
    case boost::beast::http::verb::head:
      return HttpRequestMethod::kHead;
    default:
      return HttpRequestMethod::kGet;
  }
}

boost::beast::http::verb HttpServer::HttpRequestMethodToBeastHttpVerb(
    HttpRequestMethod method) {
  switch (method) {
    case HttpRequestMethod::kGet:
      return boost::beast::http::verb::get;
    case HttpRequestMethod::kPost:
      return boost::beast::http::verb::post;
    case HttpRequestMethod::kPut:
      return boost::beast::http::verb::put;
    case HttpRequestMethod::kDelete:
      return boost::beast::http::verb::delete_;
    case HttpRequestMethod::kPatch:
      return boost::beast::http::verb::patch;
    case HttpRequestMethod::kHead:
      return boost::beast::http::verb::head;
  }
  return boost::beast::http::verb::get;
}
