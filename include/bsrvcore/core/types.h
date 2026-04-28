/**
 * @file types.h
 * @brief Public aliases for core Asio, Beast, and JSON concepts.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-06
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_CORE_TYPES_H_
#define BSRVCORE_CORE_TYPES_H_

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <atomic>
#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <memory>

namespace bsrvcore {

/** @brief Namespace alias to the underlying Boost.JSON implementation. */
namespace json = boost::json;

/** @brief Canonical Asio io_context type used by bsrvcore. */
using IoContext = boost::asio::io_context;
/** @brief Canonical type-erased executor used across bsrvcore APIs. */
using AnyExecutor = boost::asio::any_io_executor;
/** @brief Semantic alias for bsrvcore I/O executor hand-off points. */
using IoExecutor = AnyExecutor;
/** @brief Executor type produced directly by an io_context. */
using IoContextExecutor = IoContext::executor_type;
/** @brief Work guard type used to keep io_context instances alive. */
using IoWorkGuard = boost::asio::executor_work_guard<IoContextExecutor>;
/** @brief TCP protocol family alias used by server/client transports. */
using Tcp = boost::asio::ip::tcp;
/** @brief TCP endpoint alias used by listening and client dial targets. */
using TcpEndpoint = Tcp::endpoint;
/** @brief TCP resolver alias used by client connection setup. */
using TcpResolver = Tcp::resolver;
/** @brief Resolver result sequence for TCP endpoints. */
using TcpResolverResults = TcpResolver::results_type;
/** @brief SSL context alias used by HTTPS and WSS APIs. */
using SslContext = boost::asio::ssl::context;
/** @brief Shared SSL context handle used by public APIs. */
using SslContextPtr = std::shared_ptr<SslContext>;
/** @brief Atomic shared pointer alias used for lock-free snapshot publication. */
template <typename T>
using AtomicSharedPtr = std::atomic<std::shared_ptr<T>>;
/** @brief Steady timer alias used by connection/runtime timeouts. */
using SteadyTimer = boost::asio::steady_timer;
/** @brief Beast flat buffer alias used by HTTP and WebSocket parsing. */
using FlatBuffer = boost::beast::flat_buffer;
/** @brief Beast TCP stream alias used by plain socket transports. */
using TcpStream = boost::beast::tcp_stream;
/** @brief Generic Beast SSL stream template alias. */
template <typename Stream>
using BasicSslStream = boost::beast::ssl_stream<Stream>;
/** @brief Canonical SSL stream alias used by HTTPS and WSS transports. */
using SslStream = BasicSslStream<TcpStream>;
/** @brief Canonical HTTP field storage used by bsrvcore message types. */
using HttpFields = boost::beast::http::fields;
/** @brief Canonical HTTP string body used by bsrvcore message types. */
using HttpStringBody = boost::beast::http::string_body;
/** @brief HTTP method enum alias used by route and client APIs. */
using HttpVerb = boost::beast::http::verb;
/** @brief HTTP status enum alias used by response APIs. */
using HttpStatus = boost::beast::http::status;
/** @brief HTTP header-name enum alias used by response/request helpers. */
using HttpField = boost::beast::http::field;
/** @brief Canonical HTTP request type used by bsrvcore APIs. */
using HttpRequest = boost::beast::http::request<HttpStringBody, HttpFields>;
/** @brief Canonical HTTP response type used by bsrvcore APIs. */
using HttpResponse = boost::beast::http::response<HttpStringBody, HttpFields>;
/** @brief Request-header-only type used by staged callbacks and snapshots. */
using HttpRequestHeader = boost::beast::http::request_header<HttpFields>;
/** @brief Response-header-only type used by staged callbacks and snapshots. */
using HttpResponseHeader = boost::beast::http::response_header<HttpFields>;
/** @brief Canonical request parser used by server-side connection code. */
using HttpRequestParser = boost::beast::http::request_parser<HttpStringBody>;
/** @brief Empty HTTP body type used by header-only writes. */
using HttpEmptyBody = boost::beast::http::empty_body;
/** @brief Empty HTTP response type used by header-only write queues. */
using HttpEmptyResponse = boost::beast::http::response<HttpEmptyBody>;
/** @brief Serializer type for empty HTTP responses. */
using HttpEmptyResponseSerializer =
    boost::beast::http::response_serializer<HttpEmptyBody>;
/** @brief Generic Beast WebSocket stream template alias. */
template <typename Stream>
using BasicWebSocketStream = boost::beast::websocket::stream<Stream>;
/** @brief Plain WebSocket stream alias used by client/server transports. */
using WebSocketStream = BasicWebSocketStream<TcpStream>;
/** @brief TLS WebSocket stream alias used by secure client transports. */
using SecureWebSocketStream = BasicWebSocketStream<SslStream>;
/** @brief WebSocket handshake response alias. */
using WebSocketResponse = boost::beast::websocket::response_type;
/** @brief WebSocket close payload alias. */
using WebSocketCloseReason = boost::beast::websocket::close_reason;
/** @brief WebSocket ping/pong payload alias. */
using WebSocketPingData = boost::beast::websocket::ping_data;

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

#endif  // BSRVCORE_CORE_TYPES_H_
