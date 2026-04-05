# Client tasks (HTTP/HTTPS + uploads + SSE)

This chapter maps to:

- `include/bsrvcore/connection/client/http_client_task.h`
- `include/bsrvcore/connection/client/put_generator.h`
- `include/bsrvcore/connection/client/multipart_generator.h`
- `include/bsrvcore/connection/client/http_sse_client_task.h`
- `include/bsrvcore/connection/client/sse_event_parser.h`

## HttpClientTask

`HttpClientTask` runs **one** HTTP/HTTPS request.

Its executor model is explicit:

- I/O factories accept `IoContextExecutor`
- a second overload lets you provide a dedicated callback executor
- the simpler overload defaults callback delivery back onto the same executor
- `thread_pool::executor_type` is intentionally not accepted here

It reports progress using stage callbacks:

- `kConnected`
- `kHeader`
- `kChunk`
- `kDone`

Typical usage:

1. Create a task.
2. Optionally edit the request through `task->Request()` or `task->SetJson(...)`.
3. Register callbacks.
4. Call `Start()`.
5. Run your executor.

Minimal example:

```cpp
bsrvcore::IoContext ioc;

auto task = bsrvcore::HttpClientTask::CreateFromUrl(
  ioc.get_executor(),
  "http://127.0.0.1:8080/ping",
  bsrvcore::HttpVerb::get);

task->OnDone([](const bsrvcore::HttpClientResult& r) {
  if (r.ec || r.cancelled) {
    return;
  }
  // r.response is valid on success
});

task->Start();
ioc.run();
```

If you want callbacks on another `io_context`, use the two-executor overload:

```cpp
bsrvcore::IoContext io_ioc;
bsrvcore::IoContext callback_ioc;

auto task = bsrvcore::HttpClientTask::CreateHttp(
  io_ioc.get_executor(),
  callback_ioc.get_executor(),
  "127.0.0.1",
  "8080",
  "/ping",
  bsrvcore::HttpVerb::get);
```

JSON helpers:

- `task->SetJson(value)` serializes the request body and sets `Content-Type: application/json`
- `result.ParseJsonBody(out)` parses `result.response.body()` and returns a `bsrvcore::JsonErrorCode`
- `result.TryParseJsonBody(out)` is the bool-only convenience wrapper

### HTTPS note

- `CreateFromUrl(executor, "https://...", ...)` now works without passing a TLS context.
- The library creates one shared client TLS context internally and loads system trust roots.
- If you need custom trust or client certificates, use the overload that takes `SslContextPtr`.

## PutGenerator and MultipartGenerator

These are **request builders** on top of `HttpClientTask`.

They do a first async phase:

1. read one or more `FileReader` objects
2. build the final request body in memory
3. return a ready-to-start `HttpClientTask`

So the split is:

- `PutGenerator` / `MultipartGenerator`: prepare the request
- `HttpClientTask`: execute the request

### PutGenerator example

```cpp
bsrvcore::IoContext ioc;

auto reader = bsrvcore::FileReader::Create(
  "/tmp/blob.bin",
  ioc.get_executor());

auto client = bsrvcore::PutGenerator::CreateFromUrl(
  ioc.get_executor(),
  "http://127.0.0.1:8080/upload");

client->SetFile(reader).SetContentType("application/octet-stream");

client->AsyncCreateTask(
  [](std::error_code ec, std::shared_ptr<bsrvcore::HttpClientTask> task) {
    if (ec || !task) {
      return;
    }

    task->OnDone([](const bsrvcore::HttpClientResult& r) {
      // final HTTP result
    });
    task->Start();
  });

ioc.run();
```

### MultipartGenerator example

```cpp
bsrvcore::IoContext ioc;

auto file_reader = bsrvcore::FileReader::Create(
  "/tmp/photo.jpg",
  ioc.get_executor());

auto client = bsrvcore::MultipartGenerator::CreateFromUrl(
  ioc.get_executor(),
  "http://127.0.0.1:8080/form");

client->AddTextPart("title", "demo")
    .AddFilePart("upload", file_reader, "photo.jpg", "image/jpeg");

client->AsyncCreateTask(
  [](std::error_code ec, std::shared_ptr<bsrvcore::HttpClientTask> task) {
    if (!ec && task) {
      task->Start();
    }
  });
```

Rules:

- `PutGenerator` builds one `PUT` request from one file.
- `MultipartGenerator` builds one `POST multipart/form-data` request.
- generator factories also take `io_context::executor_type`, matching `HttpClientTask`
- file parts require a field `name`
- default multipart filename is the file path basename
- default multipart content type is `application/octet-stream`
- generators are shared-only and created through `Create*`
- the ready callback receives an **unstarted** `HttpClientTask`

## Async waiters behind upload builders

Upload builders internally use async waiters to converge multiple file-read
callbacks before creating the final `HttpClientTask`.

If you need that pattern directly for your own code, see
[Async waiters](async-waiters.md).

## HttpSseClientTask

`HttpSseClientTask` is for **Server-Sent Events (SSE)**.

SSE is one long HTTP response where the server keeps sending text lines.
The client reads the stream again and again.

bsrvcore uses a pull loop:

1. `Start(cb)` connects, writes request, reads and validates response header.
2. Call `Next(cb)` repeatedly to pull more body bytes.

- Only one `Next()` can run at a time.
- `Next()` returns `result.chunk`: the new bytes since your last `Next()`.
- Normal stream end is `result.eof=true`.

Example with `SseEventParser`:

```cpp
bsrvcore::IoContext ioc;
bsrvcore::SseEventParser parser;

auto task = bsrvcore::HttpSseClientTask::CreateFromUrl(
  ioc.get_executor(),
  "http://127.0.0.1:8080/events");

std::function<void()> pull_next;
pull_next = [task, &parser, &pull_next]() {
  task->Next([&parser, &pull_next](const bsrvcore::HttpSseClientResult& r) {
    if (r.ec || r.cancelled || r.eof) {
      return;
    }

    for (const auto& ev : parser.Feed(r.chunk)) {
      // ev.event / ev.data / ev.id / ev.retry_ms
    }

    pull_next();
  });
};

task->Start([&pull_next](const bsrvcore::HttpSseClientResult& r) {
  if (!r.ec && !r.cancelled) {
    pull_next();
  }
});

ioc.run();
```

## Cancellation

Both HTTP and SSE client tasks support `Cancel()`.

Cancel is best-effort and closes the socket. After you cancel, callbacks may
still fire once with `cancelled=true`.

## WebSocket tasks

For WebSocket task APIs, upgrade semantics, and stage-1 limitations, see
[WebSocket tasks](websocket-tasks.md).

## Example sources

- `examples/client-tasks/http_request.cc`
- `examples/client-tasks/sse_events.cc`

Next: [Examples](examples.md)
