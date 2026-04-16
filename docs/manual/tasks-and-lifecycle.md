# Tasks and lifecycle

This chapter maps to `include/bsrvcore/connection/server/http_server_task.h`.

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
- JSON helpers: `ParseRequestJson()`, `TryParseRequestJson()`, `SetJson()`
- Request body wrappers: `MultipartParser::Create(*task)`, `PutProcessor::Create(*task)`
- Response helpers: `SetBody()`, `AppendBody()`, `SetField()`, `SetKeepAlive()`
- Async helpers: `Post()`, `Dispatch()`, `FuturedPost()`, `SetTimer()`
- I/O helpers: `PostToIoContext()`, `DispatchToIoContext()`, `GetIoExecutor()`, `GetEndpointExecutors()`, `GetGlobalExecutors()`
- Connection control: `IsAvailable()`, `DoClose()`, `DoCycle()`

JSON helper semantics:

- `ParseRequestJson(out)` parses `GetRequest().body()` and returns a `bsrvcore::JsonErrorCode`
- `TryParseRequestJson(out)` is the bool-only convenience wrapper
- `SetJson(value)` serializes JSON into the response body and sets `Content-Type: application/json`

Async execution semantics:

- `Post()` callbacks run on the server worker pool.
- `Dispatch()` targets the same worker pool, but may run inline when already on that executor.
- `SetTimer()` uses the connection-local I/O executor for timeout tracking, then runs callback on the worker pool.
- `GetExecutor()` can be used when integrating with APIs that require `bsrvcore::IoExecutor`.
- `PostToIoContext()` / `DispatchToIoContext()` target the connection-local I/O executor.
- The request lifecycle itself advances in-place without extra internal dispatch hops.
- Pre runs first, then the main handler task is created, and post starts only after the last `HttpServerTask` reference is released.

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

## API Signature Migration (v0.16.0+)

Starting with bsrvcore v0.15.0, handler and aspect virtual functions accept
`const std::shared_ptr<T>&` (const reference) instead of `std::shared_ptr<T>`
(by value) for performance optimization.

### Updating Custom Handlers

If you have a custom class inheriting from `HttpRequestHandler`:

**Before (v0.14.x)**:

```cpp
class MyHandler : public bsrvcore::HttpRequestHandler {
  void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
    task->SetBody("response");
  }
};
```

**After (v0.16.0+)**:

```cpp
class MyHandler : public bsrvcore::HttpRequestHandler {
  void Service(const std::shared_ptr<bsrvcore::HttpServerTask>& task) override {
    task->SetBody("response");  // Same logic, just change signature
  }
};
```

### Updating Custom Aspects

Similarly for `HttpRequestAspectHandler`:

**Before (v0.14.x)**:

```cpp
class MyAspect : public bsrvcore::HttpRequestAspectHandler {
  void PreService(std::shared_ptr<bsrvcore::HttpPreServerTask> task) override { }
  void PostService(std::shared_ptr<bsrvcore::HttpPostServerTask> task) override { }
};
```

**After (v0.16.0+)**:

```cpp
class MyAspect : public bsrvcore::HttpRequestAspectHandler {
  void PreService(const std::shared_ptr<bsrvcore::HttpPreServerTask>& task) override { }
  void PostService(const std::shared_ptr<bsrvcore::HttpPostServerTask>& task) override { }
};
```

### Lambda Handlers: No Changes Needed

Lambda handlers in application code **automatically adapt** to the new signature:

```cpp
// This lambda automatically works with the new const-ref signature
server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/ping",
  [](const std::shared_ptr<bsrvcore::HttpServerTask>& task) {
    task->SetBody("pong");
  });
```

### When to Capture Explicitly

If your handler needs to retain the request lifetime (e.g., for CPU-bound or
async work), capture by move:

```cpp
[task = std::move(task)]() {
  // Now the shared_ptr is copied into the lambda
  // Handler lifetime extended until lambda completes
}
```

See [Request Lifecycle](../design/request-lifecycle.md#cpu-tasks-and-async-semantics)
for detailed examples.

## WebSocket upgrade entry

`HttpTaskBase` provides `IsWebSocketRequest()` to detect HTTP upgrade shape.

`HttpServerTask` provides `UpgradeToWebSocket(...)` as the WebSocket handoff
entry in this stage. It records upgrade intent during service execution, then
the post-task lifecycle deleter creates and starts the concrete
`WebSocketServerTask` after the HTTP task is released. The exact task APIs and
current stage limitations are documented in
[WebSocket tasks](websocket-tasks.md).

When a request is upgraded, the final response path uses a header-only
submission state so the HTTP body is dropped without switching the task into
manual connection management.

Next: [Request body processing](request-body-processing.md).
