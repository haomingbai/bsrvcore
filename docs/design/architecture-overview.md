# Architecture Overview

bsrvcore is split into a small number of subsystems that deliberately keep the
public API compact while hiding most control-flow complexity in `src/`.

## Major Subsystems

- `include/bsrvcore/`: public API surface exposed to library users.
- `include/bsrvcore-c/`: standalone public C ABI surface.
- `src/core/`: `HttpServer` startup, accept loop, executor publication, and
  shutdown coordination.
- `src/connection/server/`: per-connection request parsing, task lifecycle, and
  response writing.
- `src/route/`: route tree registration, matching, mounting, and aspect
  collection.
- `src/session/`: session context lookup, timeout refresh, and stale-entry
  cleanup.
- `src/connection/client/`: outbound HTTP/SSE client tasks.
- `src/c_binding/`: C ABI adapters, task wrappers, callback adapters, and
  packaging metadata for the standalone C binding.
- `src/bsrvrun/`: YAML-driven runtime assembly and plugin loading.

## Component View

```mermaid
flowchart LR
    Client["HTTP Client"] --> Server["HttpServer"]
    CApp["C Application"] --> CAbi["libbsrvcore-c.so"]
    CAbi --> ServerLib["libbsrvcore.so"]
    Runtime["bsrvrun YAML + plugins"] --> Server
    ServerLib --> Server
    Server --> Accept["Accept loop / endpoint runtimes"]
    Accept --> Conn["HttpServerConnection"]
    Conn --> Route["HttpRouteTable"]
    Route --> Tasks["Pre / Service / Post tasks"]
    Tasks --> Session["SessionMap"]
    Tasks --> Logger["Logger"]
    Tasks --> Response["Response writer"]
    ClientTasks["HttpClientTask / HttpSseClientTask"] --> External["External HTTP/SSE services"]
```

## Ownership Model

- `HttpServer` owns route table, session map, thread-pool resources, and
  endpoint runtimes.
- Each accepted socket becomes one `HttpServerConnection`.
- One request creates one shared `HttpTaskSharedState`, then three lightweight
  task views may be built on top of it: pre, service, and post.
- Session state is stored in `SessionMap` and shared through `Context`.
- The C binding owns only ABI adapters and wrapper structs; it does not own a
  second server runtime.

## Foreign ABI Surface

The C binding is intentionally kept outside `include/bsrvcore/` and outside the
main C++ package metadata.

Why:

- C and C++ headers have different stability and packaging requirements.
- `c_devel` must stay independent from the C++ `devel` package.
- A separate `libbsrvcore-c.so` lets `c_devel` own its own CMake and
  `pkg-config` metadata without duplicating the C++ package metadata.

See also:

- [C bindings and packaging](c-bindings-and-packaging.md)

## Source Anchors

- Server runtime: [`src/core/http_server_accept.cc`](../../src/core/http_server_accept.cc),
  [`src/core/http_server_runtime.cc`](../../src/core/http_server_runtime.cc)
- Request lifecycle:
  [`src/connection/server/http_server_task_lifecycle.cc`](../../src/connection/server/http_server_task_lifecycle.cc)
- Routing:
  [`src/route/http_route_table_registry.cc`](../../src/route/http_route_table_registry.cc),
  [`src/route/http_route_table_match.cc`](../../src/route/http_route_table_match.cc)
- Sessions:
  [`src/session/session_map.cc`](../../src/session/session_map.cc)
- Runtime container:
  [`src/bsrvrun/server_builder.cc`](../../src/bsrvrun/server_builder.cc)
- C binding bridge:
  [`src/c_binding/server.cc`](../../src/c_binding/server.cc),
  [`src/c_binding/task.cc`](../../src/c_binding/task.cc),
  [`src/c_binding/callback_adapters.cc`](../../src/c_binding/callback_adapters.cc)
