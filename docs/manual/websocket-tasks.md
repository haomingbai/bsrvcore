# WebSocket tasks

This chapter maps to:

- `include/bsrvcore/connection/websocket/websocket_task_base.h`
- `include/bsrvcore/connection/server/websocket_server_task.h`
- `include/bsrvcore/connection/client/websocket_client_task.h`
- `include/bsrvcore/connection/server/http_server_task.h` (upgrade entry)

## Status

WebSocket client/server transport is implemented for both plain and TLS modes.

Implemented now:

- Shared task/handler abstraction (`WebSocketTaskBase`, `WebSocketHandler`)
- Server-side request detection (`HttpTaskBase::IsWebSocketRequest()`)
- Server-side upgrade entry (`HttpServerTask::UpgradeToWebSocket(...)`)
- Client-side `ws://` and `wss://` factories
- Real message read loop
- Real `WriteMessage(...)` delivery
- Real control-frame delivery for ping / pong / close
- Handshake HTTP callback passthrough through `OnHttpDone(...)`

## Core types

`WebSocketHandler` callback contract:

- `OnOpen()`
- `OnReadMessage(const WebSocketMessage&)`
- `OnError(error_code, message)`
- `OnClose(error_code)`

`WebSocketMessage` contains:

- `payload`
- `binary`

`WebSocketControlKind` contains:

- `kPing`
- `kPong`
- `kClose`

## Server-side usage

Use normal routing, then detect and upgrade:

```cpp
server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/ws",
  [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
    if (!task->IsWebSocketRequest()) {
      task->SetBody("not websocket upgrade");
      return;
    }

    const bool upgraded = task->UpgradeToWebSocket(
      bsrvcore::AllocateUnique<MyWebSocketHandler>());

    if (!upgraded) {
      task->SetBody("upgrade failed");
      return;
    }
  });
```

Important:

- `UpgradeToWebSocket(...)` only records upgrade intent in service phase.
- `IsWebSocketUpgradeMarked()` can be used to query whether upgrade intent was
  already recorded on current `HttpServerTask`.
- The concrete `WebSocketServerTask` is created and started in the
  post-task lifecycle deleter after the HTTP chain (including post aspects)
  is fully released.
- Upgrade path uses a header-only submission state to discard the HTTP body
  and keep the WebSocket upgrade lifecycle separate from manual management.
- Keep behavior aligned with first-stage limitations above.

## Client-side usage

Create a task and configure request before `Start()`:

```cpp
bsrvcore::IoContext ioc;

auto task = bsrvcore::WebSocketClientTask::CreateFromUrl(
  ioc.get_executor(),
  "ws://127.0.0.1:8080/ws",
  bsrvcore::AllocateUnique<MyWebSocketHandler>());

task->OnHttpDone([](const bsrvcore::HttpClientResult& r) {
  // Handshake result and response headers.
});

task->Start();
ioc.run();
```

`CreateFromUrl` accepts both `ws://` and `wss://` directly.
For `wss://`, the default overload creates a shared client TLS context and
loads system trust roots. If you need custom TLS settings, use the overload
that takes `SslContextPtr`.

If you want cookie-managed creation, use `HttpClientSession` websocket
factories (`CreateWebSocketHttp/CreateWebSocketHttps/CreateWebSocketFromUrl`).
Session-bound tasks auto-inject matching cookies before handshake and
auto-sync `Set-Cookie` from handshake response headers.

## Testing notes

WebSocket has dedicated tests in all three layers:

- Unit: `tests/general/connection/unit_*websocket*`
- Integration: `tests/general/connection/integration_websocket_tasks_test.cc`
- Stress: `tests/stress/connection/stress_websocket_tasks_test.cc`

Run WebSocket-only tests:

```bash
ctest --test-dir build -R websocket --output-on-failure
```

Run by label:

```bash
ctest --test-dir build -R websocket -L unit --output-on-failure
ctest --test-dir build -R websocket -L integration --output-on-failure
ctest --test-dir build -R websocket -L stress --output-on-failure
```

Next: [Examples](examples.md)
