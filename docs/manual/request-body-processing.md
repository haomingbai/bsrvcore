# Request body processing

This chapter maps to:

- `include/bsrvcore/connection/server/multipart_parser.h`
- `include/bsrvcore/connection/server/put_processor.h`
- `include/bsrvcore/file/file_writer.h`

## One-sentence idea

Use `MultipartParser` or `PutProcessor` when you already have a buffered HTTP
request body and want to bridge it into `FileWriter` objects.

## Important model

These wrappers are still **post-read** helpers:

- bsrvcore reads the HTTP request body through the normal Beast/Asio path
- your handler receives a buffered body in `task->GetRequest().body()`
- `MultipartParser` and `PutProcessor` work on that buffered body

This is not a streaming multipart parser.

Today both wrappers sit on top of the file module:

- multipart file parts become `std::shared_ptr<FileWriter>`
- PUT request bodies become one `std::shared_ptr<FileWriter>`
- legacy `AsyncDumpToDisk(...)` helpers still work, but they now delegate to
  `FileWriter::AsyncWriteToDisk(...)`

## Which wrapper to use

- `MultipartParser`: for `multipart/form-data`
- `PutProcessor`: for raw `PUT` request bodies

The shared factory is the normal choice inside a route handler:

```cpp
auto multipart = bsrvcore::MultipartParser::Create(*task);
auto put = bsrvcore::PutProcessor::Create(*task);
```

You can also construct from `HttpRequest` directly:

```cpp
auto multipart = bsrvcore::MultipartParser::Create(request);
auto put = bsrvcore::PutProcessor::Create(request);
```

If you want different work/callback executors, use the two-executor overload:

```cpp
boost::asio::io_context work_ioc;
boost::asio::io_context callback_ioc;

auto put = bsrvcore::PutProcessor::Create(
  request,
  work_ioc.get_executor(),
  callback_ioc.get_executor());
```

## MultipartParser basics

Utility methods:

- `GetPartCount()`
- `GetPartType(part_idx)`
- `IsFile(part_idx)`
- `GetFileWriter(part_idx)`

Typical pattern:

```cpp
server->AddRouteEntry(
    bsrvcore::HttpRequestMethod::kPost, "/upload",
    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
      namespace http = boost::beast::http;

      auto multipart = bsrvcore::MultipartParser::Create(*task);

      std::size_t file_part = multipart->GetPartCount();
      for (std::size_t i = 0; i < multipart->GetPartCount(); ++i) {
        if (multipart->IsFile(i)) {
          file_part = i;
          break;
        }
      }

      if (file_part == multipart->GetPartCount()) {
        task->GetResponse().result(http::status::bad_request);
        task->SetBody("no file part found");
        return;
      }

      auto writer = multipart->GetFileWriter(file_part);
      if (!writer || !writer->AsyncWriteToDisk("/tmp/upload.bin")) {
        task->GetResponse().result(http::status::bad_request);
        task->SetBody("multipart body is not dumpable");
        return;
      }

      task->GetResponse().result(http::status::accepted);
      task->SetBody("file dump scheduled");
    });
```

Notes:

- `GetPartCount() == 0` usually means the request is not valid multipart data.
- `IsFile()` is based on the `filename=` parameter in `Content-Disposition`.
- `GetPartType()` returns the part `Content-Type`, or an empty string when the
  header is missing.
- `GetFileWriter(...)` returns `nullptr` for non-file parts.
- `AsyncDumpToDisk(...)` is still available for older code.

## PutProcessor basics

`PutProcessor` treats the full request body as one payload and exposes it as a
single `FileWriter`.

If you need the final write result, use the writer directly:

```cpp
server->AddRouteEntry(
    bsrvcore::HttpRequestMethod::kPut, "/blob",
    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
      namespace http = boost::beast::http;

      auto put = bsrvcore::PutProcessor::Create(*task);
      auto writer = put->GetFileWriter();
      if (!writer ||
          !writer->AsyncWriteToDisk(
              "/tmp/blob.bin",
              [task](std::shared_ptr<bsrvcore::FileWritingState> state) {
                const bool ok = state && !state->ec;
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

If you only want backward-compatible behavior, the old wrapper still exists:

```cpp
put->AsyncDumpToDisk("/tmp/blob.bin", [](bool ok) {
  // compatibility callback
});
```

## Return values

Both wrappers use the same rule:

- the immediate `bool` return means: "was async work scheduled?"
- the later state or callback result means: "did the actual file operation
  succeed?"

## File I/O backend

File work still uses blocking filesystem calls internally, but those calls are
dispatched asynchronously onto the selected work executor.

This does **not** change the server socket backend:

- network I/O still uses the existing server I/O path
- blocking file work consumes the selected work executor threads
- completion callbacks hop back to the selected callback executor

Next: [SSE server](sse-server.md)
