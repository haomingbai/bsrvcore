# HTTP server

This chapter maps to `include/bsrvcore/core/http_server.h`.

## One-sentence idea

`HttpServer` listens on a TCP port, finds a route for each request, runs aspects + handler, and writes a response.

## Create and run

```cpp
auto server = std::make_unique<bsrvcore::HttpServer>(4);
server->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8080}, 2);

if (!server->Start()) {
  // failed to start
}

server->Stop();
```

- `HttpServer(thread_num)` sets worker thread count (core=max=thread_num).
- `AddListen(endpoint, io_threads)` declares endpoint I/O shard count.
- `Start()` materializes endpoint shards and starts accept/read/write loops.

To fully control runtime behavior, use `HttpServerRuntimeOptions`:

```cpp
bsrvcore::HttpServerRuntimeOptions options;
options.core_thread_num = 4;
options.max_thread_num = 8;
options.fast_queue_capacity = 256;
options.thread_clean_interval = 60000;
options.task_scan_interval = 100;
options.suspend_time = 1;
options.has_max_connection = true;
options.max_connection = 4096;

auto server = std::make_unique<bsrvcore::HttpServer>(options);
```

`HttpServerExecutorOptions` is kept as a compatibility alias.

## Configuration

Common knobs (milliseconds unless noted):

- `SetHeaderReadExpiry(ms)`
- `SetDefaultReadExpiry(ms)` / `SetReadExpiry(method, url, ms)`
- `SetDefaultWriteExpiry(ms)` / `SetWriteExpiry(method, url, ms)`
- `SetDefaultMaxBodySize(bytes)` / `SetMaxBodySize(method, url, bytes)`
- `SetKeepAliveTimeout(ms)`

Example:

```cpp
server->SetDefaultReadExpiry(5000)
      ->SetDefaultWriteExpiry(5000)
      ->SetDefaultMaxBodySize(1024 * 1024)
      ->SetKeepAliveTimeout(15000);
```

## TLS (HTTPS)

To enable HTTPS on one endpoint, provide a shared TLS context to that endpoint:

```cpp
auto tls_ctx = std::make_shared<boost::asio::ssl::context>(
  boost::asio::ssl::context::tls_server);

// configure certificates / private key on *tls_ctx
server->AddListen(
  {boost::asio::ip::make_address("0.0.0.0"), 8443},
  2,
  tls_ctx);
```

Notes:

- `AddListen(endpoint, io_threads)` remains plain HTTP.
- `AddListen(endpoint, io_threads, tls_ctx)` makes only that endpoint HTTPS.
- Reusing the same `std::shared_ptr<context>` across endpoints shares one TLS configuration.
- Passing different `std::shared_ptr<context>` objects isolates TLS state per endpoint.

## Posting work and timers

The server can also run background work:

- `Post(fn)` / `Dispatch(fn)` / `FuturedPost(fn, ...)`
- `SetTimer(timeout_ms, fn)`
- `PostToIoContext(fn)` / `DispatchToIoContext(fn)`

These are useful when you want to do work on the server's executors.
For example: update shared state, schedule cleanup, or run a periodic task.

Execution model:

- `SetTimer` uses `io_context` for timing, then dispatches callback to worker pool.
- `Post` always dispatches callback to worker pool.
- `Dispatch` targets the worker pool too, but may run inline when already on that executor.
- `PostToIoContext` / `DispatchToIoContext` target endpoint I/O executors.
- `GetExecutor()` returns a type-erased `boost::asio::any_io_executor` backed by the worker pool.
- `GetIoExecutor()` returns one selected endpoint I/O executor.
- `GetEndpointExecutors(idx)` returns the executor list for one endpoint.
- `GetGlobalExecutors()` returns the flattened executor list for all endpoints.

Next: [Routing](routing.md).
