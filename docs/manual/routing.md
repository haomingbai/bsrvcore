# Routing

This chapter maps to:

- `include/bsrvcore/http_server.h`
- `include/bsrvcore/http_request_method.h`
- `include/bsrvcore/http_request_handler.h`

## AddRouteEntry

Register a route by **method + path pattern**.

```cpp
server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/ping",
  [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
    task->SetBody("pong");
  });
```

## Path parameters

Route patterns can contain parameters in braces.
This is the common "path template" pattern:

- `/hello/{name}`

Inside the handler, bsrvcore stores the parameter values in a map keyed by
parameter name:

```cpp
const auto& params = task->GetPathParameters();
// params.at("name") is the captured value
```

You can also read one parameter directly:

```cpp
const auto* name = task->GetPathParameter("name");
```

## Exclusive routes

`AddExclusiveRouteEntry()` gives the route higher priority than parameter routes at the same path.

Use this when you have both:

- `/users/me` (exclusive)
- `/users/{id}` (parameter)

## BluePrint

`BluePrint` lets you build a route tree first and mount it later under a path
prefix.

```cpp
auto blue_print = bsrvcore::BluePrintFactory::Create();
blue_print.AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
  [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
    const auto* id = task->GetPathParameter("id");
    task->SetBody(id == nullptr ? "missing" : *id);
  });

server->AddBluePrint("/api", std::move(blue_print));
```

`ReuseableBluePrint` is the reusable variant. It deep-clones its handlers and
aspects on each mount, so one blueprint can be mounted at multiple prefixes.

## Default handler

A fallback handler runs when no route matches.
This is useful for a simple 404 page, or for API version fallbacks:

```cpp
server->SetDefaultHandler([](std::shared_ptr<bsrvcore::HttpServerTask> task) {
  task->GetResponse().result(boost::beast::http::status::not_found);
  task->SetBody("Not found\n");
});
```

## Current location

`task->GetCurrentLocation()` returns the matched concrete request path.

`task->GetRouteTemplate()` returns the matched route template string.

Tip: you can log it in an aspect to see which route matched.

Next: [Tasks and lifecycle](tasks-and-lifecycle.md).
