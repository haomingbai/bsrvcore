# HTTP server

This chapter maps to `include/bsrvcore/http_server.h`.

## One-sentence idea

`HttpServer` listens on a TCP port, finds a route for each request, runs aspects + handler, and writes a response.

## Create and run

```cpp
auto server = std::make_unique<bsrvcore::HttpServer>(4);
server->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8080});

if (!server->Start(2)) {
  // failed to start
}

server->Stop();
```

- `HttpServer(thread_num)` sets worker thread count (core=max=thread_num).
- `Start(thread_count)` starts I/O threads (accept + read/write).

To fully control worker executor behavior, use `HttpServerExecutorOptions`:

```cpp
bsrvcore::HttpServerExecutorOptions options;
options.core_thread_num = 4;
options.max_thread_num = 8;
options.fast_queue_capacity = 256;
options.thread_clean_interval = 60000;
options.task_scan_interval = 100;
options.suspend_time = 1;

auto server = std::make_unique<bsrvcore::HttpServer>(options);
```

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

To enable HTTPS on the server, provide an SSL context:

```cpp
boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12_server);
// configure certificates / private key on ctx
server->SetSslContext(std::move(ctx));
```

You can disable HTTPS later with `UnsetSslContext()`.

## Posting work and timers

The server can also run background work:

- `Post(fn)` / `FuturedPost(fn, ...)`
- `SetTimer(timeout_ms, fn)`

These are useful when you want to do work on the server's executors.
For example: update shared state, schedule cleanup, or run a periodic task.

Execution model:

- `SetTimer` uses `io_context` for timing, then dispatches callback to worker pool.
- `Post` always dispatches callback to worker pool.
- `GetExecutor()` returns a type-erased `boost::asio::any_io_executor` backed by the worker pool.
- For I/O-related operations, use `GetIoContext()`.

Next: [Routing](routing.md).
