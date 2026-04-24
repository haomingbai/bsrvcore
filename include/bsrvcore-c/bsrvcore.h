#pragma once

#ifndef BSRVCORE_C_BINDING_BSRVCORE_H_
#define BSRVCORE_C_BINDING_BSRVCORE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file bsrvcore.h
 * @brief Public C bindings for bsrvcore HTTP server APIs.
 *
 * The C binding is a thin ABI-stable wrapper over the C++ HTTP server APIs.
 * It intentionally keeps callback phases separate:
 * - @ref bsrvcore_http_pre_server_task_t for pre-aspect callbacks
 * - @ref bsrvcore_http_server_task_t for route handlers
 * - @ref bsrvcore_http_post_server_task_t for post-aspect callbacks
 *
 * All task handles exposed by this header are borrowed views. They are only
 * valid during the active callback that received them.
 *
 * Request header and body getters return borrowed pointers into the current
 * request storage. They do not allocate or copy. The returned buffers are not
 * guaranteed to be NUL-terminated; always consume them together with the
 * returned length.
 */

/** @brief Opaque HTTP server handle. */
typedef struct bsrvcore_server bsrvcore_server_t;

/** @brief Opaque task handle passed to route handlers. */
typedef struct bsrvcore_http_server_task bsrvcore_http_server_task_t;

/** @brief Opaque task handle passed to pre-aspect callbacks. */
typedef struct bsrvcore_http_pre_server_task bsrvcore_http_pre_server_task_t;

/** @brief Opaque task handle passed to post-aspect callbacks. */
typedef struct bsrvcore_http_post_server_task bsrvcore_http_post_server_task_t;

/**
 * @brief Result codes returned by the C binding APIs.
 */
typedef enum bsrvcore_result {
  /** @brief Operation completed successfully. */
  BSRVCORE_RESULT_OK = 0,
  /** @brief One or more input arguments were invalid. */
  BSRVCORE_RESULT_INVALID_ARGUMENT = 1,
  /** @brief Memory allocation failed. */
  BSRVCORE_RESULT_ALLOCATION_FAILURE = 2,
  /** @brief Requested data or route was not found. */
  BSRVCORE_RESULT_NOT_FOUND = 3,
  /** @brief Input parsing failed. */
  BSRVCORE_RESULT_PARSE_ERROR = 4,
  /** @brief The server was already running. */
  BSRVCORE_RESULT_ALREADY_RUNNING = 5,
  /** @brief The server was not running. */
  BSRVCORE_RESULT_NOT_RUNNING = 6,
  /** @brief The underlying operation failed. */
  BSRVCORE_RESULT_OPERATION_FAILED = 7,
  /** @brief An unexpected internal error occurred. */
  BSRVCORE_RESULT_INTERNAL_ERROR = 8
} bsrvcore_result_t;

/**
 * @brief Supported HTTP methods for route and aspect registration.
 */
typedef enum bsrvcore_http_method {
  /** @brief HTTP GET. */
  BSRVCORE_HTTP_METHOD_GET = 0,
  /** @brief HTTP POST. */
  BSRVCORE_HTTP_METHOD_POST = 1,
  /** @brief HTTP PUT. */
  BSRVCORE_HTTP_METHOD_PUT = 2,
  /** @brief HTTP DELETE. */
  BSRVCORE_HTTP_METHOD_DELETE = 3,
  /** @brief HTTP PATCH. */
  BSRVCORE_HTTP_METHOD_PATCH = 4,
  /** @brief HTTP HEAD. */
  BSRVCORE_HTTP_METHOD_HEAD = 5
} bsrvcore_http_method_t;

/**
 * @brief Stateless route callback.
 * @param task Borrowed route task handle for the current request.
 */
typedef void (*bsrvcore_http_handler_fn)(bsrvcore_http_server_task_t* task);

/**
 * @brief Stateful route callback.
 * @param task Borrowed route task handle for the current request.
 * @param ctx User-provided context pointer registered with the route.
 *
 * The binding stores @p ctx as-is and never frees it.
 */
typedef void (*bsrvcore_http_handler_ctx_fn)(bsrvcore_http_server_task_t* task,
                                             void* ctx);

/**
 * @brief Stateless pre-aspect callback.
 * @param task Borrowed pre-aspect task handle for the current request.
 */
typedef void (*bsrvcore_http_pre_aspect_fn)(
    bsrvcore_http_pre_server_task_t* task);

/**
 * @brief Stateful pre-aspect callback.
 * @param task Borrowed pre-aspect task handle for the current request.
 * @param ctx User-provided context pointer registered with the aspect.
 *
 * The binding stores @p ctx as-is and never frees it.
 */
typedef void (*bsrvcore_http_pre_aspect_ctx_fn)(
    bsrvcore_http_pre_server_task_t* task, void* ctx);

/**
 * @brief Stateless post-aspect callback.
 * @param task Borrowed post-aspect task handle for the current request.
 */
typedef void (*bsrvcore_http_post_aspect_fn)(
    bsrvcore_http_post_server_task_t* task);

/**
 * @brief Stateful post-aspect callback.
 * @param task Borrowed post-aspect task handle for the current request.
 * @param ctx User-provided context pointer registered with the aspect.
 *
 * The binding stores @p ctx as-is and never frees it.
 */
typedef void (*bsrvcore_http_post_aspect_ctx_fn)(
    bsrvcore_http_post_server_task_t* task, void* ctx);

/**
 * @brief Create a new HTTP server instance.
 * @param worker_threads Worker thread count for the server. Passing `0` uses
 *        bsrvcore's default worker-thread configuration.
 * @param out_server Receives the newly created server on success.
 * @return `BSRVCORE_RESULT_OK` on success, otherwise an error code.
 */
bsrvcore_result_t bsrvcore_server_create(size_t worker_threads,
                                         bsrvcore_server_t** out_server);

/**
 * @brief Destroy an HTTP server instance.
 * @param server Server handle to destroy. Passing `NULL` is allowed.
 *
 * Destroying a running server is allowed; the binding stops and releases the
 * underlying resources during teardown.
 */
void bsrvcore_server_destroy(bsrvcore_server_t* server);

/**
 * @brief Start serving requests.
 * @param server Server handle.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_start(bsrvcore_server_t* server);

/**
 * @brief Stop a running server.
 * @param server Server handle.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_stop(bsrvcore_server_t* server);

/**
 * @brief Query whether the server is currently running.
 * @param server Server handle.
 * @param out_running Receives `1` when running, otherwise `0`.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_is_running(const bsrvcore_server_t* server,
                                             int* out_running);

/**
 * @brief Add a listen endpoint.
 * @param server Server handle.
 * @param host Host or IP address to bind.
 * @param port TCP port to bind.
 * @param io_threads I/O thread count for the listener. Passing `0` uses
 *        bsrvcore defaults.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_add_listen(bsrvcore_server_t* server,
                                             const char* host, uint16_t port,
                                             size_t io_threads);

/**
 * @brief Set the default header read timeout in milliseconds.
 * @param server Server handle.
 * @param timeout_ms Timeout in milliseconds.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_set_header_read_expiry_ms(
    bsrvcore_server_t* server, size_t timeout_ms);

/**
 * @brief Set the default request body read timeout in milliseconds.
 * @param server Server handle.
 * @param timeout_ms Timeout in milliseconds.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_set_default_read_expiry_ms(
    bsrvcore_server_t* server, size_t timeout_ms);

/**
 * @brief Set the default response write timeout in milliseconds.
 * @param server Server handle.
 * @param timeout_ms Timeout in milliseconds.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_set_default_write_expiry_ms(
    bsrvcore_server_t* server, size_t timeout_ms);

/**
 * @brief Set the default maximum request body size.
 * @param server Server handle.
 * @param max_body_size Maximum accepted request body size in bytes.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_set_default_max_body_size(
    bsrvcore_server_t* server, size_t max_body_size);

/**
 * @brief Set the keep-alive timeout in milliseconds.
 * @param server Server handle.
 * @param timeout_ms Timeout in milliseconds.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_set_keep_alive_timeout_ms(
    bsrvcore_server_t* server, size_t timeout_ms);

/**
 * @brief Set a route-specific request body read timeout.
 * @param server Server handle.
 * @param method HTTP method for the target route.
 * @param route Route pattern.
 * @param timeout_ms Timeout in milliseconds.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_set_route_read_expiry_ms(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    size_t timeout_ms);

/**
 * @brief Set a route-specific response write timeout.
 * @param server Server handle.
 * @param method HTTP method for the target route.
 * @param route Route pattern.
 * @param timeout_ms Timeout in milliseconds.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_set_route_write_expiry_ms(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    size_t timeout_ms);

/**
 * @brief Set a route-specific maximum request body size.
 * @param server Server handle.
 * @param method HTTP method for the target route.
 * @param route Route pattern.
 * @param max_body_size Maximum accepted request body size in bytes.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_set_route_max_body_size(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    size_t max_body_size);

/**
 * @brief Register a stateless route handler.
 * @param server Server handle.
 * @param method HTTP method for the route.
 * @param route Route pattern.
 * @param handler Stateless callback to invoke for matching requests.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_add_route(bsrvcore_server_t* server,
                                            bsrvcore_http_method_t method,
                                            const char* route,
                                            bsrvcore_http_handler_fn handler);

/**
 * @brief Register a stateful route handler.
 * @param server Server handle.
 * @param method HTTP method for the route.
 * @param route Route pattern.
 * @param handler Stateful callback to invoke for matching requests.
 * @param ctx User-provided context pointer bound to the route.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_add_route_with_ctx(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    bsrvcore_http_handler_ctx_fn handler, void* ctx);

/**
 * @brief Register global stateless aspects.
 * @param server Server handle.
 * @param pre_aspect Optional pre-aspect callback. Pass `NULL` to omit it.
 * @param post_aspect Optional post-aspect callback. Pass `NULL` to omit it.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_add_global_aspect(
    bsrvcore_server_t* server, bsrvcore_http_pre_aspect_fn pre_aspect,
    bsrvcore_http_post_aspect_fn post_aspect);

/**
 * @brief Register global stateful aspects.
 * @param server Server handle.
 * @param pre_aspect Optional pre-aspect callback. Pass `NULL` to omit it.
 * @param post_aspect Optional post-aspect callback. Pass `NULL` to omit it.
 * @param ctx User-provided context pointer bound to the aspect pair.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_add_global_aspect_with_ctx(
    bsrvcore_server_t* server, bsrvcore_http_pre_aspect_ctx_fn pre_aspect,
    bsrvcore_http_post_aspect_ctx_fn post_aspect, void* ctx);

/**
 * @brief Register stateless method-global aspects.
 * @param server Server handle.
 * @param method HTTP method to match.
 * @param pre_aspect Optional pre-aspect callback. Pass `NULL` to omit it.
 * @param post_aspect Optional post-aspect callback. Pass `NULL` to omit it.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_add_method_global_aspect(
    bsrvcore_server_t* server, bsrvcore_http_method_t method,
    bsrvcore_http_pre_aspect_fn pre_aspect,
    bsrvcore_http_post_aspect_fn post_aspect);

/**
 * @brief Register stateful method-global aspects.
 * @param server Server handle.
 * @param method HTTP method to match.
 * @param pre_aspect Optional pre-aspect callback. Pass `NULL` to omit it.
 * @param post_aspect Optional post-aspect callback. Pass `NULL` to omit it.
 * @param ctx User-provided context pointer bound to the aspect pair.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_add_method_global_aspect_with_ctx(
    bsrvcore_server_t* server, bsrvcore_http_method_t method,
    bsrvcore_http_pre_aspect_ctx_fn pre_aspect,
    bsrvcore_http_post_aspect_ctx_fn post_aspect, void* ctx);

/**
 * @brief Register stateless subtree aspects rooted at a route.
 * @param server Server handle.
 * @param method HTTP method to match.
 * @param route Route pattern.
 * @param pre_aspect Optional pre-aspect callback. Pass `NULL` to omit it.
 * @param post_aspect Optional post-aspect callback. Pass `NULL` to omit it.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_add_route_aspect(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    bsrvcore_http_pre_aspect_fn pre_aspect,
    bsrvcore_http_post_aspect_fn post_aspect);

/**
 * @brief Register stateful subtree aspects rooted at a route.
 * @param server Server handle.
 * @param method HTTP method to match.
 * @param route Route pattern.
 * @param pre_aspect Optional pre-aspect callback. Pass `NULL` to omit it.
 * @param post_aspect Optional post-aspect callback. Pass `NULL` to omit it.
 * @param ctx User-provided context pointer bound to the aspect pair.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_add_route_aspect_with_ctx(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    bsrvcore_http_pre_aspect_ctx_fn pre_aspect,
    bsrvcore_http_post_aspect_ctx_fn post_aspect, void* ctx);

/**
 * @brief Register stateless terminal aspects for an exact route hit.
 * @param server Server handle.
 * @param method HTTP method to match.
 * @param route Route pattern.
 * @param pre_aspect Optional pre-aspect callback. Pass `NULL` to omit it.
 * @param post_aspect Optional post-aspect callback. Pass `NULL` to omit it.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_add_terminal_aspect(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    bsrvcore_http_pre_aspect_fn pre_aspect,
    bsrvcore_http_post_aspect_fn post_aspect);

/**
 * @brief Register stateful terminal aspects for an exact route hit.
 * @param server Server handle.
 * @param method HTTP method to match.
 * @param route Route pattern.
 * @param pre_aspect Optional pre-aspect callback. Pass `NULL` to omit it.
 * @param post_aspect Optional post-aspect callback. Pass `NULL` to omit it.
 * @param ctx User-provided context pointer bound to the aspect pair.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_server_add_terminal_aspect_with_ctx(
    bsrvcore_server_t* server, bsrvcore_http_method_t method, const char* route,
    bsrvcore_http_pre_aspect_ctx_fn pre_aspect,
    bsrvcore_http_post_aspect_ctx_fn post_aspect, void* ctx);

/**
 * @brief Read a request header from a route task.
 * @param task Borrowed route task handle.
 * @param key Header field name.
 * @param out_value Receives a borrowed pointer to the header value.
 * @param out_value_len Receives the header value length.
 * @return `BSRVCORE_RESULT_OK` on success or `BSRVCORE_RESULT_NOT_FOUND` when
 *         the header does not exist.
 */
bsrvcore_result_t bsrvcore_http_server_task_get_request_header(
    const bsrvcore_http_server_task_t* task, const char* key,
    const char** out_value, size_t* out_value_len);

/**
 * @brief Read the request body from a route task.
 * @param task Borrowed route task handle.
 * @param out_body Receives a borrowed pointer to the request body.
 * @param out_body_len Receives the request body length.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_server_task_get_request_body(
    const bsrvcore_http_server_task_t* task, const char** out_body,
    size_t* out_body_len);

/**
 * @brief Set the HTTP response status code on a route task.
 * @param task Borrowed route task handle.
 * @param status_code HTTP status code.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_server_task_set_status(
    bsrvcore_http_server_task_t* task, int status_code);

/**
 * @brief Set or overwrite a response header on a route task.
 * @param task Borrowed route task handle.
 * @param key Header field name.
 * @param value Header field value.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_server_task_set_response_header(
    bsrvcore_http_server_task_t* task, const char* key, const char* value);

/**
 * @brief Replace the response body on a route task.
 * @param task Borrowed route task handle.
 * @param body Response body bytes.
 * @param len Response body length in bytes.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_server_task_set_response_body(
    bsrvcore_http_server_task_t* task, const char* body, size_t len);

/**
 * @brief Append bytes to the response body on a route task.
 * @param task Borrowed route task handle.
 * @param body Response body bytes to append.
 * @param len Byte count to append.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_server_task_append_response_body(
    bsrvcore_http_server_task_t* task, const char* body, size_t len);

/**
 * @brief Set the complete response on a route task.
 * @param task Borrowed route task handle.
 * @param status_code HTTP status code.
 * @param content_type Optional `Content-Type` header. Pass `NULL` to leave it
 *        unchanged.
 * @param body Response body bytes.
 * @param len Response body length in bytes.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_server_task_set_response(
    bsrvcore_http_server_task_t* task, int status_code,
    const char* content_type, const char* body, size_t len);

/**
 * @brief Read a request header from a pre-aspect task.
 * @param task Borrowed pre-aspect task handle.
 * @param key Header field name.
 * @param out_value Receives a borrowed pointer to the header value.
 * @param out_value_len Receives the header value length.
 * @return `BSRVCORE_RESULT_OK` on success or `BSRVCORE_RESULT_NOT_FOUND` when
 *         the header does not exist.
 */
bsrvcore_result_t bsrvcore_http_pre_server_task_get_request_header(
    const bsrvcore_http_pre_server_task_t* task, const char* key,
    const char** out_value, size_t* out_value_len);

/**
 * @brief Read the request body from a pre-aspect task.
 * @param task Borrowed pre-aspect task handle.
 * @param out_body Receives a borrowed pointer to the request body.
 * @param out_body_len Receives the request body length.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_pre_server_task_get_request_body(
    const bsrvcore_http_pre_server_task_t* task, const char** out_body,
    size_t* out_body_len);

/**
 * @brief Set the HTTP response status code on a pre-aspect task.
 * @param task Borrowed pre-aspect task handle.
 * @param status_code HTTP status code.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_pre_server_task_set_status(
    bsrvcore_http_pre_server_task_t* task, int status_code);

/**
 * @brief Set or overwrite a response header on a pre-aspect task.
 * @param task Borrowed pre-aspect task handle.
 * @param key Header field name.
 * @param value Header field value.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_pre_server_task_set_response_header(
    bsrvcore_http_pre_server_task_t* task, const char* key, const char* value);

/**
 * @brief Replace the response body on a pre-aspect task.
 * @param task Borrowed pre-aspect task handle.
 * @param body Response body bytes.
 * @param len Response body length in bytes.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_pre_server_task_set_response_body(
    bsrvcore_http_pre_server_task_t* task, const char* body, size_t len);

/**
 * @brief Append bytes to the response body on a pre-aspect task.
 * @param task Borrowed pre-aspect task handle.
 * @param body Response body bytes to append.
 * @param len Byte count to append.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_pre_server_task_append_response_body(
    bsrvcore_http_pre_server_task_t* task, const char* body, size_t len);

/**
 * @brief Set the complete response on a pre-aspect task.
 * @param task Borrowed pre-aspect task handle.
 * @param status_code HTTP status code.
 * @param content_type Optional `Content-Type` header. Pass `NULL` to leave it
 *        unchanged.
 * @param body Response body bytes.
 * @param len Response body length in bytes.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_pre_server_task_set_response(
    bsrvcore_http_pre_server_task_t* task, int status_code,
    const char* content_type, const char* body, size_t len);

/**
 * @brief Read a request header from a post-aspect task.
 * @param task Borrowed post-aspect task handle.
 * @param key Header field name.
 * @param out_value Receives a borrowed pointer to the header value.
 * @param out_value_len Receives the header value length.
 * @return `BSRVCORE_RESULT_OK` on success or `BSRVCORE_RESULT_NOT_FOUND` when
 *         the header does not exist.
 */
bsrvcore_result_t bsrvcore_http_post_server_task_get_request_header(
    const bsrvcore_http_post_server_task_t* task, const char* key,
    const char** out_value, size_t* out_value_len);

/**
 * @brief Read the request body from a post-aspect task.
 * @param task Borrowed post-aspect task handle.
 * @param out_body Receives a borrowed pointer to the request body.
 * @param out_body_len Receives the request body length.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_post_server_task_get_request_body(
    const bsrvcore_http_post_server_task_t* task, const char** out_body,
    size_t* out_body_len);

/**
 * @brief Set the HTTP response status code on a post-aspect task.
 * @param task Borrowed post-aspect task handle.
 * @param status_code HTTP status code.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_post_server_task_set_status(
    bsrvcore_http_post_server_task_t* task, int status_code);

/**
 * @brief Set or overwrite a response header on a post-aspect task.
 * @param task Borrowed post-aspect task handle.
 * @param key Header field name.
 * @param value Header field value.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_post_server_task_set_response_header(
    bsrvcore_http_post_server_task_t* task, const char* key, const char* value);

/**
 * @brief Replace the response body on a post-aspect task.
 * @param task Borrowed post-aspect task handle.
 * @param body Response body bytes.
 * @param len Response body length in bytes.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_post_server_task_set_response_body(
    bsrvcore_http_post_server_task_t* task, const char* body, size_t len);

/**
 * @brief Append bytes to the response body on a post-aspect task.
 * @param task Borrowed post-aspect task handle.
 * @param body Response body bytes to append.
 * @param len Byte count to append.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_post_server_task_append_response_body(
    bsrvcore_http_post_server_task_t* task, const char* body, size_t len);

/**
 * @brief Set the complete response on a post-aspect task.
 * @param task Borrowed post-aspect task handle.
 * @param status_code HTTP status code.
 * @param content_type Optional `Content-Type` header. Pass `NULL` to leave it
 *        unchanged.
 * @param body Response body bytes.
 * @param len Response body length in bytes.
 * @return `BSRVCORE_RESULT_OK` on success.
 */
bsrvcore_result_t bsrvcore_http_post_server_task_set_response(
    bsrvcore_http_post_server_task_t* task, int status_code,
    const char* content_type, const char* body, size_t len);

#ifdef __cplusplus
}
#endif

#endif  // BSRVCORE_C_BINDING_BSRVCORE_H_
