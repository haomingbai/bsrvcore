# Request body processing

This chapter maps to:

- `include/bsrvcore/connection/server/multipart_parser.h`
- `include/bsrvcore/connection/server/put_processor.h`

## One-sentence idea

Use `MultipartParser` or `PutProcessor` when you already have a request body in
`HttpRequest::body()` and want to inspect it or dump it to disk asynchronously.

## Important model

These wrappers are **post-read** helpers:

- bsrvcore still reads the HTTP request body through the normal Beast/Asio path
- your handler receives a buffered body in `task->GetRequest().body()`
- `MultipartParser` and `PutProcessor` work on that buffered body

This is not a streaming multipart parser.

## Which wrapper to use

- `MultipartParser`: for `multipart/form-data`
- `PutProcessor`: for raw `PUT` request bodies

The task-based constructor is the normal choice inside a route handler:

```cpp
bsrvcore::MultipartParser multipart(*task);
bsrvcore::PutProcessor put(*task);
```

You can also construct from `HttpRequest` directly:

```cpp
bsrvcore::MultipartParser multipart(request);
bsrvcore::PutProcessor put(request);
```

If you want the callback-based async overloads from a plain `HttpRequest`,
provide an executor:

```cpp
boost::asio::io_context ioc;
bsrvcore::PutProcessor put(request, ioc.get_executor());
```

## MultipartParser basics

Utility methods:

- `GetPartCount()`
- `GetPartType(part_idx)`
- `IsFile(part_idx)`

Typical pattern:

```cpp
server->AddRouteEntry(
    bsrvcore::HttpRequestMethod::kPost, "/upload",
    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
      namespace http = boost::beast::http;

      bsrvcore::MultipartParser multipart(*task);

      std::size_t file_part = multipart.GetPartCount();
      for (std::size_t i = 0; i < multipart.GetPartCount(); ++i) {
        if (multipart.IsFile(i)) {
          file_part = i;
          break;
        }
      }

      if (file_part == multipart.GetPartCount()) {
        task->GetResponse().result(http::status::bad_request);
        task->SetBody("no file part found");
        return;
      }

      task->GetResponse().result(http::status::accepted);
      task->SetBody("file dump scheduled");

      if (!multipart.AsyncDumpToDisk(file_part, "/tmp/upload.bin")) {
        task->GetResponse().result(http::status::bad_request);
        task->SetBody("multipart body is not dumpable");
      }
    });
```

Notes:

- `GetPartCount() == 0` usually means the request is not valid multipart data.
- `IsFile()` is based on the `filename=` parameter in `Content-Disposition`.
- `GetPartType()` returns the part `Content-Type`, or an empty string when the
  header is missing.
- Only file parts are dumpable through `AsyncDumpToDisk(...)`.

## PutProcessor basics

`PutProcessor` treats the full request body as one payload.

If you need the final write result, use the callback overload and capture the
task:

```cpp
server->AddRouteEntry(
    bsrvcore::HttpRequestMethod::kPut, "/blob",
    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
      namespace http = boost::beast::http;

      bsrvcore::PutProcessor put(*task);
      if (!put.AsyncDumpToDisk(
              "/tmp/blob.bin",
              [task](bool ok) {
                task->GetResponse().result(
                    ok ? http::status::ok
                       : http::status::internal_server_error);
                task->SetBody(ok ? "saved" : "save failed");
              })) {
        task->GetResponse().result(http::status::bad_request);
        task->SetBody("request is not a dumpable PUT body");
      }
    });
```

Why capture `task`?

- the dump finishes later
- the callback may need to update the response
- keeping `task` alive delays final response write until the callback returns

If you do **not** care about the final write result, use the no-callback
overload:

```cpp
server->AddRouteEntry(
    bsrvcore::HttpRequestMethod::kPut, "/fire-and-forget",
    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
      namespace http = boost::beast::http;

      bsrvcore::PutProcessor put(*task);
      if (!put.AsyncDumpToDisk("/tmp/blob.bin")) {
        task->GetResponse().result(http::status::bad_request);
        task->SetBody("request is not a dumpable PUT body");
        return;
      }

      task->GetResponse().result(http::status::accepted);
      task->SetBody("dump scheduled");
    });
```

## Return values

Both wrappers use the same rule:

- the immediate `bool` return means: "is this request/part valid and was async
  work scheduled?"
- the callback `bool` means: "did the file dump actually complete
  successfully?"

So a call can return `true` immediately and still report `false` later in the
callback if file open/write/close fails.

## File I/O backend

File dumping uses Boost.Asio file objects. On Linux, bsrvcore enables Asio's
`io_uring` file support for these operations.

This does **not** change the server socket backend:

- network I/O still uses the existing server runtime path
- Linux networking is still not forced onto `io_uring`
- this feature only affects request-body file dumping

## Build note

Because Asio file support is enabled through Linux `io_uring`, `liburing` is a
required build dependency for bsrvcore.

Next: [SSE server](sse-server.md).
