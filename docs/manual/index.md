# bsrvcore Manual (Simple English)

This manual follows the public headers under `include/bsrvcore/` and the source layout under `src/`.

This is a **tutorial**: it teaches the ideas and the recommended way to use bsrvcore.

For complete API details (types, overloads, edge cases), use **Doxygen** generated from the public headers.

If you need to understand the internal implementation instead of the public
usage model, see the maintainer notes in [../design/index.md](../design/index.md).

## Learning path

Read in this order:

1. [Getting started](getting-started.md) — Goal: build the project and run a minimal server.
2. [HTTP server](http-server.md) — Goal: understand how to start/stop, configure timeouts/limits, and enable TLS.
3. [Routing](routing.md) — Goal: map method + path to a handler, including path parameters.
4. [Tasks and lifecycle](tasks-and-lifecycle.md) — Goal: understand the 3-step request pipeline and how async handlers work.
5. [Request body processing](request-body-processing.md) — Goal: inspect multipart/PUT bodies and dump them to disk asynchronously.
6. [SSE server](sse-server.md) — Goal: stream SSE data and heartbeats from a handler.
7. [Aspects (AOP)](aspects.md) — Goal: add before/after logic (middleware style) in a safe, repeatable way.
8. [Sessions, context, cookies](sessions-context-cookies.md) — Goal: store per-request and per-session data.
9. [Logging](logging.md) — Goal: plug in your logger and log from handlers.
10. [Client tasks (HTTP/HTTPS + SSE)](client-tasks.md) — Goal: call other services (HTTP) or consume SSE streams.
11. [Examples](examples.md) — Goal: run the sample programs.
12. [Testing](testing.md) — Goal: build and run tests locally and in CI.
13. [Benchmarking](benchmarking.md) — Goal: measure `HttpServer` throughput, latency, and stability under multiple pressure levels.
14. [Linux I/O model choice](linux-io-model-choice.md) — Goal: understand why `epoll` stays the default over `io_uring` for broad Linux compatibility.
15. [bsrvrun runtime container](bsrvrun.md) — Goal: run server from YAML + plugin factories.

## Public umbrella header

If you want a single include for most server-side features, use:

```cpp
#include <bsrvcore/bsrvcore.h>
```

For smaller builds, include only the headers you use (for example `http_server.h`, `http_client_task.h`).
