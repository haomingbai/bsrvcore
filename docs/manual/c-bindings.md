# C bindings

This chapter explains the standalone C ABI exposed by `include/bsrvcore-c/bsrvcore.h`.

## What the C binding is

The C binding is a thin wrapper over the same HTTP server runtime used by the
C++ API.

Use it when:

- Your application is written in C.
- You want a stable `extern "C"` API surface.
- You only need the HTTP server, routing, request/response access, and aspects.

The C API does **not** use the C++ umbrella header.

Use:

```c
#include <bsrvcore-c/bsrvcore.h>
```

All exported symbols use the `bsrvcore_` prefix.

## Build and install

Enable the C binding and keep the shared library build:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DBSRVCORE_BUILD_C_BINDINGS=ON
cmake --build build --parallel
sudo cmake --install build
```

`BSRVCORE_BUILD_C_BINDINGS` is enabled by default in this repository.

## Installed files and packages

The runtime installs two shared libraries:

- `libbsrvcore.so`
- `libbsrvcore-c.so`

`libbsrvcore-c.so` is the standalone C ABI wrapper library.

The C development package installs:

- `include/bsrvcore-c/bsrvcore.h`
- `lib*/cmake/bsrvcore_c/bsrvcore_cConfig.cmake`
- `share/pkgconfig/bsrvcore-c.pc`

Package split:

- `runtime`: shared runtime library and runtime tools
- `devel`: C++ headers and C++ package metadata
- `c_devel`: C header and C package metadata

The standalone `c_devel` package is designed for **shared-library** installs.
Static standalone consumption is not supported, because keeping `c_devel`
independent from the C++ `devel` package requires the C API to stay on its own
shared wrapper library.

## Minimal server

Source: [examples/c-binding/quick_start.c](../../examples/c-binding/quick_start.c)

```c
#include <bsrvcore-c/bsrvcore.h>

#include <stdio.h>

static void hello_handler(bsrvcore_http_server_task_t* task) {
  bsrvcore_http_server_task_set_response(
      task, 200, "text/plain; charset=utf-8", "Hello, bsrvcore C binding.\n",
      sizeof("Hello, bsrvcore C binding.\n") - 1);
}

int main(void) {
  bsrvcore_server_t* server = NULL;

  if (bsrvcore_server_create(4, &server) != BSRVCORE_RESULT_OK) {
    return 1;
  }

  if (bsrvcore_server_add_route(server, BSRVCORE_HTTP_METHOD_GET, "/hello",
                                hello_handler) != BSRVCORE_RESULT_OK ||
      bsrvcore_server_add_listen(server, "0.0.0.0", 8080, 2) !=
          BSRVCORE_RESULT_OK ||
      bsrvcore_server_start(server) != BSRVCORE_RESULT_OK) {
    bsrvcore_server_destroy(server);
    return 1;
  }

  puts("Listening on http://0.0.0.0:8080/hello");
  (void)getchar();

  (void)bsrvcore_server_stop(server);
  bsrvcore_server_destroy(server);
  return 0;
}
```

## Server lifecycle and configuration

Main lifecycle calls:

- `bsrvcore_server_create`
- `bsrvcore_server_destroy`
- `bsrvcore_server_start`
- `bsrvcore_server_stop`
- `bsrvcore_server_is_running`
- `bsrvcore_server_add_listen`

Server-level configuration currently includes:

- header read timeout
- default read timeout
- default write timeout
- default max body size
- keep-alive timeout
- route-specific read timeout
- route-specific write timeout
- route-specific max body size

## Handlers

The C binding supports both stateless and stateful route callbacks.

Stateless:

```c
static void hello_handler(bsrvcore_http_server_task_t* task) {
  bsrvcore_http_server_task_set_response(
      task, 200, "text/plain", "hello", 5);
}
```

Stateful:

```c
struct HandlerContext {
  const char* prefix;
};

static void echo_handler(bsrvcore_http_server_task_t* task, void* ctx) {
  struct HandlerContext* handler_ctx = ctx;
  bsrvcore_http_server_task_set_response_header(
      task, "X-Prefix", handler_ctx->prefix);
}
```

Register them with:

- `bsrvcore_server_add_route`
- `bsrvcore_server_add_route_with_ctx`

`ctx` lifetime is owned by the caller. The binding stores the raw pointer and
does not free it.

## Request and response access

Each callback receives a task handle that matches its phase:

- `bsrvcore_http_server_task_t`
- `bsrvcore_http_pre_server_task_t`
- `bsrvcore_http_post_server_task_t`

Task handles are borrowed views. Do not store them after the callback returns.

Request read helpers:

- `*_get_request_header`
- `*_get_request_body`

Important rules:

- returned pointers are borrowed
- returned buffers are not guaranteed to be NUL-terminated
- always use the returned length
- returned data is only valid during the current callback

Response write helpers:

- `*_set_status`
- `*_set_response_header`
- `*_set_response_body`
- `*_append_response_body`
- `*_set_response`

`*_set_response` is the simplest way to set `status + optional content-type + body`
in one call.

## Aspects (AOP)

The C API keeps pre/service/post task types separate on purpose.

Supported aspect registration shapes:

- global aspect
- method-global aspect
- route subtree aspect
- route terminal aspect
- stateless callback pair
- stateful callback pair with `void* ctx`

Use:

- `bsrvcore_server_add_global_aspect`
- `bsrvcore_server_add_global_aspect_with_ctx`
- `bsrvcore_server_add_method_global_aspect`
- `bsrvcore_server_add_method_global_aspect_with_ctx`
- `bsrvcore_server_add_route_aspect`
- `bsrvcore_server_add_route_aspect_with_ctx`
- `bsrvcore_server_add_terminal_aspect`
- `bsrvcore_server_add_terminal_aspect_with_ctx`

`bsrvcore_server_add_route_aspect*` registers subtree aspects rooted at the
given route. `bsrvcore_server_add_terminal_aspect*` keeps the old exact-route
behavior.

Callback types:

- `bsrvcore_http_pre_aspect_fn`
- `bsrvcore_http_pre_aspect_ctx_fn`
- `bsrvcore_http_post_aspect_fn`
- `bsrvcore_http_post_aspect_ctx_fn`

## Consume from CMake

Recommended:

```cmake
find_package(bsrvcore_c CONFIG REQUIRED)

add_executable(app main.c)
target_link_libraries(app PRIVATE bsrvcore::bsrvcore_c)
```

The installed `bsrvcore_c` package points to `libbsrvcore-c.so`.

## Consume from pkg-config

You can also compile a C program directly:

```bash
cc main.c $(pkg-config --cflags --libs bsrvcore-c)
```

## Current scope and limits

The current C binding covers:

- HTTP server lifecycle
- server timeouts and body-size configuration
- routing
- request header/body reads
- response status/header/body writes
- aspects

It does not currently expose the full C++ surface such as:

- WebSocket server tasks
- TLS configuration APIs
- session/context helpers
- logging helpers
- streaming-specific low-level task operations

For the exact exported API, use the Doxygen output generated from
[include/bsrvcore-c/bsrvcore.h](../../include/bsrvcore-c/bsrvcore.h).
