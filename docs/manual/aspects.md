# Aspects (AOP)

This chapter maps to `include/bsrvcore/route/http_request_aspect_handler.h` and
`include/bsrvcore/core/http_server.h`.

## What is an aspect?

An aspect is a **before/after helper** around your route handler.

If you know "middleware", it is the same idea:

- **Pre** runs *before* your route handler.
- **Post** runs *after* your route handler.

Use aspects for work you do in many places: logging, timing, auth checks,
adding headers.

## Global aspects

Run for every request:

```cpp
server->AddGlobalAspect(
  [](const std::shared_ptr<bsrvcore::HttpPreServerTask>& task) {
    task->SetField("X-Request-Start", "1");
  },
  [](const std::shared_ptr<bsrvcore::HttpPostServerTask>& task) {
    task->SetField("X-Request-End", "1");
  });
```

You can also register global aspects for a specific method:

- `AddGlobalAspect(method, aspect)`
- `AddGlobalAspect(method, pre_fn, post_fn)`

## Subtree aspects

`AddAspect()` now registers a **subtree** aspect.

Attach it to a prefix route and it will run for:

- that route itself, when it resolves to a real handler
- descendant routes below that prefix

It does **not** run when routing falls back to the default handler.

```cpp
server->AddAspect(
  bsrvcore::HttpRequestMethod::kGet,
  "/api",
  [](const std::shared_ptr<bsrvcore::HttpPreServerTask>& task) {
    task->SetField("X-Subtree-Aspect", "pre");
  },
  [](const std::shared_ptr<bsrvcore::HttpPostServerTask>& task) {
    task->SetField("X-Subtree-Aspect", "post");
  });
```

## Terminal aspects

`AddTerminalAspect()` registers an aspect only for the **final matched route**.

Use it when you want the old exact-route behavior:

```cpp
server->AddTerminalAspect(
  bsrvcore::HttpRequestMethod::kGet,
  "/api/ping",
  [](const std::shared_ptr<bsrvcore::HttpPreServerTask>& task) {
    task->SetField("X-Terminal-Aspect", "pre");
  },
  [](const std::shared_ptr<bsrvcore::HttpPostServerTask>& task) {
    task->SetField("X-Terminal-Aspect", "post");
  });
```

## Execution order

- Pre runs in the same order as the collected chain.
- Post runs in the reverse order.

Collection order is:

1. global aspects
2. method-global aspects
3. matched subtree aspects from root to leaf
4. terminal aspects on the final matched route

For a request to `/api/ping`, a common chain looks like:

1. Global Pre
2. Method-global Pre
3. `/api` subtree Pre
4. `/api/ping` terminal Pre
5. Route handler
6. `/api/ping` terminal Post
7. `/api` subtree Post
8. Method-global Post
9. Global Post

Execution model notes:

- Aspects are treated as **small synchronous tasks**.
- Pre aspects run directly on the request I/O execution path in registration
  order.
- The route handler then runs immediately unless the route was registered with
  `AddComputingRouteEntry()`.
- Post aspects begin only after the last `HttpServerTask` reference is
  released, so asynchronous handler work can defer the second half of the
  lifecycle naturally.

Next: [Sessions, context, cookies](sessions-context-cookies.md).
