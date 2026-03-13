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

Inside the handler, bsrvcore stores the parameter values in a vector:

```cpp
const auto& params = task->GetPathParameters();
// params[0] is the first parameter
```

## Exclusive routes

`AddExclusiveRouteEntry()` gives the route higher priority than parameter routes at the same path.

Use this when you have both:

- `/users/me` (exclusive)
- `/users/{id}` (parameter)

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

`task->GetCurrentLocation()` returns the matched route location string.

Tip: you can log it in an aspect to see which route matched.

Next: [Tasks and lifecycle](tasks-and-lifecycle.md).
