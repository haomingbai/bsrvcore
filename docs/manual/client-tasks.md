# Client tasks (HTTP/HTTPS + SSE)

This chapter maps to:

- `include/bsrvcore/http_client_task.h`
- `include/bsrvcore/http_sse_client_task.h`
- `include/bsrvcore/sse_event_parser.h`

## HttpClientTask

`HttpClientTask` runs **one** HTTP/HTTPS request.

It reports progress using **stage callbacks**. This is a simple state-machine pattern: you get called when the request moves to the next step.

- `kConnected` (TCP connected, and TLS handshake done for HTTPS)
- `kHeader` (response header received)
- `kChunk` (body bytes, only if you register `OnChunk`)
- `kDone` (final: success/failure/cancel)

### How to use it

1. Create a task.
2. (Optional) edit the request via `task->Request()`.
3. Register callbacks.
4. Call `Start()`.
5. Run your `io_context`.

Minimal example:

```cpp
boost::asio::io_context ioc;

auto task = bsrvcore::HttpClientTask::CreateFromUrl(
  ioc.get_executor(),
  "http://127.0.0.1:8080/ping",
  boost::beast::http::verb::get);

task->OnDone([](const bsrvcore::HttpClientResult& r) {
  if (r.ec || r.cancelled) {
    return;
  }
  // r.response is valid on success
});

task->Start();
ioc.run();
```

### HTTPS note

- `CreateFromUrl(executor, "https://...", ...)` needs an SSL context.
- Use the overload that takes `boost::asio::ssl::context&`.

## HttpSseClientTask

`HttpSseClientTask` is for **Server-Sent Events (SSE)**.

SSE is one long HTTP response where the server keeps sending text lines.
The client reads the stream again and again.

bsrvcore uses a **pull loop**:

1. `Start(cb)` connects, writes request, reads and validates response header.
2. Call `Next(cb)` repeatedly to pull more body bytes.

- Only one `Next()` can run at a time.
- `Next()` returns `result.chunk`: the new bytes since your last `Next()`.
- Normal stream end is `result.eof=true`.

### How to think about Start/Next

- `Start()` is the handshake step.
- `Next()` is "read more".

Typical loop:

1. `Start()` succeeds
2. `Next()` -> parse -> `Next()` -> parse -> ...
3. stop when `ec`, `cancelled`, or `eof`

Example with `SseEventParser`:

```cpp
boost::asio::io_context ioc;
bsrvcore::SseEventParser parser;

auto task = bsrvcore::HttpSseClientTask::CreateFromUrl(
  ioc.get_executor(),
  "http://127.0.0.1:8080/events");

auto pull_next = std::make_shared<std::function<void()>>();
*pull_next = [task, &parser, pull_next]() {
  task->Next([&parser, pull_next](const bsrvcore::HttpSseClientResult& r) {
    if (r.ec || r.cancelled || r.eof) {
      return;
    }

    for (const auto& ev : parser.Feed(r.chunk)) {
      // ev.event / ev.data / ev.id / ev.retry_ms
    }

    (*pull_next)();
  });
};

task->Start([pull_next](const bsrvcore::HttpSseClientResult& r) {
  if (!r.ec && !r.cancelled) {
    (*pull_next)();
  }
});

ioc.run();
```

## Cancellation

Both tasks support `Cancel()`.

Cancel is best-effort and closes the socket.
After you cancel, callbacks may still fire once with `cancelled=true`.

Next: [Examples](examples.md).
