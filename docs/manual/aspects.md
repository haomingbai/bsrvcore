# Aspects (AOP)

This chapter maps to `include/bsrvcore/route/http_request_aspect_handler.h` and `include/bsrvcore/core/http_server.h`.

## What is an aspect?

An aspect is a **before/after helper** around your route handler.

If you know "middleware", it is the same idea:

- **Pre** runs *before* your route handler.
- **Post** runs *after* your route handler.

Use aspects for work you do in many places: logging, timing, auth checks, adding headers.

## Global aspects

Run for every request:

```cpp
server->AddGlobalAspect(
  [](std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
    task->SetField("X-Request-Start", "1");
  },
  [](std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
    task->SetField("X-Request-End", "1");
  });
```

You can also register global aspects for a specific method:

- `AddGlobalAspect(method, aspect)`
- `AddGlobalAspect(method, pre_fn, post_fn)`

## Route aspects

Attach before/after helpers to one route:

```cpp
server->AddAspect(
  bsrvcore::HttpRequestMethod::kGet,
  "/ping",
  [](std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
    task->SetField("X-Route-Aspect", "pre");
  },
  [](std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
    task->SetField("X-Route-Aspect", "post");
  });
```

## Execution order

- Pre runs in the same order as you register it.
- Post runs in the reverse order.

Execution model notes:

- Aspects are treated as **small synchronous tasks**.
- Pre aspects run directly on the request I/O execution path in registration order.
- The route handler then runs immediately unless the route was registered with `AddComputingRouteEntry()`.
- Post aspects begin only after the last `HttpServerTask` reference is released, so asynchronous handler work can defer the second half of the lifecycle naturally.

This is a common "stack" pattern:

- Pre: open resources (start timer, set context, check auth)
- Handler: do the real work
- Post: close resources (stop timer, write metrics, cleanup)

## A simple mental picture

For one request, bsrvcore runs:

1. Global Pre
2. Route Pre
3. Route handler
4. Route Post
5. Global Post

Next: [Sessions, context, cookies](sessions-context-cookies.md).
