# Maintainer Design Docs

These notes complement the tutorial manual in [`../manual/index.md`](../manual/index.md).

Use the manual when learning how to _use_ bsrvcore. Use this section when you
need to understand how the current implementation is organized and where to make
changes safely.

## Reading Order

1. [Architecture overview](architecture-overview.md)
2. [C bindings and packaging](c-bindings-and-packaging.md)
3. [Request lifecycle](request-lifecycle.md)
4. [Threading and executors](threading-and-executors.md)
5. [Routing and sessions](routing-and-sessions.md)
6. [WebSocket lifecycle](websocket-lifecycle.md)
7. [HTTP client pipeline](client-pipeline.md)

## Scope

- These docs describe the current repository structure and control flow.
- They are intentionally short and source-oriented.
- Public API details still belong to Doxygen and the tutorial manual.
