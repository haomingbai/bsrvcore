# SSE server

This chapter maps to:

- `include/bsrvcore/connection/server/http_server_task.h`
- `examples/sse/stream.cc`

## One-sentence idea

SSE is one HTTP response that stays open.
You send the header once, then keep flushing text chunks later.

## Why manual mode is needed

Normal handlers build one final response and return.
That is not enough for SSE, because the body is not finished yet.

For SSE, enable manual connection management:

```cpp
task->SetManualConnectionManagement(true);
```

After that, your handler owns the stream lifetime.
Use these APIs:

- `WriteHeader(...)` to send the HTTP response header once
- `WriteBody(...)` to flush each SSE chunk
- `SetTimer(...)` to schedule later sends
- `IsAvailable()` to detect disconnects

## Minimal SSE response

The response should use `text/event-stream`:

```cpp
bsrvcore::HttpResponseHeader header;
header.version(task->GetRequest().version());
header.result(boost::beast::http::status::ok);
header.set(boost::beast::http::field::content_type,
           "text/event-stream; charset=utf-8");
header.set(boost::beast::http::field::cache_control, "no-cache");
header.set(boost::beast::http::field::connection, "keep-alive");

task->WriteHeader(std::move(header));
```

Each SSE message ends with a blank line:

```text
event: counter
data: 1

: heartbeat

```

- `event:` names the event type
- `data:` is the payload
- `: ...` is a comment line, often used for heartbeats

## Two-timer pattern

This project example starts two one-shot timers in the same handler:

- timer A sends `event: counter` once per second
- timer B sends `: heartbeat` every 2.5 seconds

Both timers re-arm themselves after each send.
Before each write, check `task->IsAvailable()`.
If the client is gone, stop scheduling more work.
When the counter reaches the limit, mark the stream as stopped, send one final
`done` event, then close the connection shortly after.

This keeps business events and keep-alive traffic separate.

## Run the example

Build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBSRVCORE_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

Run the server:

```bash
./build/examples/example_sse_stream
```

Open the stream with curl:

```bash
curl -N http://127.0.0.1:8086/events
```

`-N` disables curl buffering, so you can see each event immediately.

## What the example sends

- one startup comment: `: stream opened`
- five counter events, one per second
- one heartbeat comment every 2.5 seconds
- one final `done` event before close

The stream ends when the counter limit is reached.
It can also stop earlier if the client disconnects or the server stops.

Next: [Aspects (AOP)](aspects.md).
