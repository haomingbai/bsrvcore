# Tasks and lifecycle

This chapter maps to `include/bsrvcore/http_server_task.h`.

## The 3-step request flow

In bsrvcore, one HTTP request goes through **three task objects**.
Think of them as three steps in one pipeline:

- `HttpPreServerTask` (pre-aspect phase)
- `HttpServerTask` (route handler phase)
- `HttpPostServerTask` (post-aspect phase)

All three share the same request/response data through `HttpTaskBase`.
So you can set headers/body in any step.

## Common task API (HttpTaskBase)

The task gives access to:

- Request/response: `GetRequest()`, `GetResponse()`
- Response helpers: `SetBody()`, `AppendBody()`, `SetField()`, `SetKeepAlive()`
- Async helpers: `Post()`, `FuturedPost()`, `SetTimer()`
- Connection control: `IsAvailable()`, `DoClose()`, `DoCycle()`

Async execution semantics:

- `Post()` callbacks run on the server worker pool.
- `SetTimer()` uses server I/O context for timeout tracking, then runs callback on the worker pool.
- For I/O-only operations, use `GetIoContext()`.

## Extending a request lifetime

`HttpServerTask` is a `std::shared_ptr`. If you keep that shared pointer and do work later (for example with `Post()` or `SetTimer()`), you can finish the response asynchronously.

Important notes:

- When you run later, the client may already be gone. Always check `task->IsAvailable()` before doing any I/O.
- If you enable manual mode, bsrvcore will **not** automatically write the final response:

```cpp
task->SetManualConnectionManagement(true);
// You are responsible for WriteHeader()/WriteBody() or DoClose()/DoCycle().
```

When manual mode is **off** (default), the normal flow is:

1. Your handler sets status/headers/body
2. bsrvcore writes the response
3. Post aspects run

Next: [SSE server](sse-server.md).
