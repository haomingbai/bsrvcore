# Examples

This chapter maps to the `examples/` folder.

Examples are organized by tutorial modules so the source tree follows the
manual learning path.

| Module | Directory | Binary examples |
| --- | --- | --- |
| Getting started | `examples/getting-started/` | `example_quick_start` |
| HTTP server | `examples/http-server/` | `example_configuration` |
| SSE server | `examples/sse/` | `example_sse_stream` |
| Routing | `examples/routing/` | `example_oop_handler` |
| Aspects | `examples/aspects/` | `example_aspect_basic` |
| Sessions and context | `examples/sessions-context/` | `example_session_context` |
| Logging | `examples/logging/` | `example_logger_custom` |
| Client tasks | `examples/client-tasks/` | `example_client_http_request`, `example_client_sse_events` |
| WebSocket tasks | `examples/websocket-tasks/` | `example_websocket_service`, `example_websocket_request` |
| bsrvrun plugins | `examples/bsrvrun/plugins/` | plugin shared libraries |

## Build examples

Examples are enabled by default in this repository.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBSRVCORE_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

## Run examples

```bash
./build/examples/getting-started/example_quick_start
./build/examples/http-server/example_configuration
./build/examples/example_sse_stream
./build/examples/routing/example_oop_handler
./build/examples/aspects/example_aspect_basic
./build/examples/logging/example_logger_custom
./build/examples/sessions-context/example_session_context
./build/examples/client-tasks/example_client_http_request
./build/examples/client-tasks/example_client_sse_events
./build/examples/websocket-tasks/example_websocket_service
./build/examples/websocket-tasks/example_websocket_request
```

See also the top-level [README.md](../../README.md) for example source links.
