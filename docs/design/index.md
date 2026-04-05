# Maintainer Design Docs

These notes complement the tutorial manual in [`../manual/index.md`](../manual/index.md).

Use the manual when learning how to _use_ bsrvcore. Use this section when you
need to understand how the current implementation is organized and where to make
changes safely.

## Reading Order

1. [Architecture overview](architecture-overview.md)
2. [Request lifecycle](request-lifecycle.md)
3. [Threading and executors](threading-and-executors.md)
4. [Routing and sessions](routing-and-sessions.md)
5. [WebSocket lifecycle](websocket-lifecycle.md)

## Scope

- These docs describe the current repository structure and control flow.
- They are intentionally short and source-oriented.
- Public API details still belong to Doxygen and the tutorial manual.
