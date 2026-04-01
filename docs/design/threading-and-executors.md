# Threading And Executors

bsrvcore uses different executor domains for different kinds of work. Most of
the implementation complexity comes from moving work between those domains
without breaking per-connection ordering.

## Executor Roles

- Worker pool executor:
  general server callbacks, route handlers wrapped as computing tasks, timer
  callbacks, and background cleanup.
- Endpoint I/O executors:
  accept loops, socket reads/writes, per-connection sequencing, and timer wait
  objects.
- Control I/O executor:
  server-level fallback executor while startup/shutdown is in progress.

## Execution Model

```mermaid
flowchart LR
    Accept["Accept / socket IO"] --> ConnExec["Connection-local IO executor"]
    ConnExec --> Lifecycle["Pre / Service / Post task entry"]
    Lifecycle --> Worker["Worker pool executor"]
    Worker --> ConnExec
    Control["Control io_context"] --> Worker
```

## Dispatch Rules

- `DispatchToConnectionExecutor()` is used for lifecycle entry so one
  connection's parse/handler/write steps stay serialized.
- `Post()` / `Dispatch()` on `HttpServer` and `HttpTaskBase` target the worker
  pool.
- `PostToIoContext()` / `DispatchToIoContext()` target a selected endpoint I/O
  executor.
- Task timers wait on an I/O executor, then post the callback back to the
  worker pool.

## REUSEPORT vs Fallback Mode

- If `SO_REUSEPORT` is available, each endpoint can build one acceptor per I/O
  shard and run one thread per shard.
- If `SO_REUSEPORT` is unavailable, bsrvcore falls back to one acceptor /
  `io_context` per endpoint and runs multiple `run()` threads on that context.
- The first endpoint decides the mode for the whole server so the published
  executor model stays consistent.

## Practical Rule For Changes

If a change touches socket state, parser state, or response write sequencing, it
should stay on the connection-local I/O executor. If it is general work that can
outlive a single socket operation, it usually belongs on the worker pool.
