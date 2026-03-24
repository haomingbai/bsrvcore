# 2026-03-25 Dispatch-Centric Runtime Technical Report

## Executive Summary

This report evaluates the final dispatch-centric runtime variant of
`bsrvcore` on the local benchmark host after the internal request lifecycle
was moved onto the connection strand / `io_context` path for short
I/O-oriented handlers.

Key conclusions:

- The final dispatch-centric variant materially outperforms the previous
  worker-pool lifecycle model on this machine.
- The best observed `http_get_static` result in this report is
  **184,870.30 RPS** at:
  `server_io_threads=6`, `server_worker_threads=30`,
  `client_concurrency=160`, `client_processes=4`,
  `wrk_threads_per_process=1`.
- A lower-latency alternative reaches **178,036.04 RPS** at:
  `server_io_threads=6`, `server_worker_threads=30`,
  `client_concurrency=80`, `client_processes=4`,
  `wrk_threads_per_process=1`, with `p95=510.99 us`.
- The default `saturated` preset is no longer the best preset for this
  runtime model. On this 20-logical-CPU host, the dispatch-centric model
  prefers **fewer I/O threads** than the old preset.
- The new peak still falls short of the 200k goal by **15,129.70 RPS**
  (**7.56%**).

In short: the runtime model change is successful, the benchmark ceiling has
moved from the low-120k range to the mid-180k range, and the 200k target now
looks plausible but not yet reached.

## Scope

This report covers:

- final runtime behavior after the dispatch-centric lifecycle changes
- system and build environment
- benchmark methodology
- parameter sweeps across I/O threads, worker threads, client concurrency, and
  load-generator topology
- cross-scenario behavior under the recommended tuned configuration
- comparison against the previous worker-pool lifecycle baseline

This report does **not** claim:

- cross-machine network performance
- TLS/HTTPS performance
- long-duration soak behavior
- NUMA or multi-socket behavior

All measurements were taken on loopback (`127.0.0.1`) using the repository
benchmark runner and bundled `wrk`.

## Code Variant Under Test

Repository state during measurement:

- commit: `c9eb85aa5c437a3e0dd57741f5a649a0cd3b2267`
- working tree: dirty
- modified files:
  - `src/connection/server/http_server_connection.cc`
  - `src/connection/server/http_server_task.cc`
  - `src/include/bsrvcore/internal/http_server_connection_impl.h`

Final runtime changes in this variant:

1. `HttpServerConnection::Run()` now enters the read path via
   `boost::asio::dispatch(...)` on the connection strand.
2. `HttpServerConnectionImpl::DoWriteResponse()` now enters the write path via
   `boost::asio::dispatch(...)` on the connection strand.
3. Internal request lifecycle transitions
   (`HttpPreServerTask::Start`, `HttpServerTask::Start`,
   `HttpPostServerTask::Start`, and aspect continuations) now use
   `conn->Dispatch(...)` instead of hopping through the worker pool.
4. The temporary internal helper `PostToIo` was removed from the final
   version.
5. The public `Post()` API semantics remain unchanged: explicit user work
   still targets the worker pool.

Design intent:

- keep short, I/O-bound handlers on the serialized connection path
- avoid unnecessary pool hops for trivial handlers
- still allow users to offload CPU-heavy work explicitly through public
  posting APIs

## Test Environment

### Hardware

- CPU: 13th Gen Intel(R) Core(TM) i9-13900H
- Logical CPUs: 20
- Sockets: 1
- NUMA nodes: 1
- L3 cache: 24 MiB
- Memory: 31 GiB

### Software

- Kernel: `Linux 6.19.6-200.fc43.x86_64`
- Distribution family: Fedora Linux 43
- Compiler: `gcc (GCC) 15.2.1 20260123 (Red Hat 15.2.1-7)`
- Build type: `Release`
- Benchmark binary: `./build/benchmarks/bsrvcore_http_benchmark`
- `wrk` path:
  `./build/_deps/bsrvcore_benchmark_wrk/src/bsrvcore_benchmark_wrk/wrk`

### Runtime-Adjacent Host Settings

- CPU governor: `performance`
- Current shell scheduling policy: `SCHED_OTHER`
- Current shell scheduling priority: `0`
- Open file descriptor limit: `524288`

Important note:

- The user adjusted CPU scheduling policy and/or scheduler-related host
  behavior outside the repository before the final benchmark series. Those
  external tuning steps are reflected in the results, but their exact system
  configuration was not captured in versioned repo state.

## Build Configuration

Observed CMake cache:

- `CMAKE_BUILD_TYPE=Release`
- `BSRVCORE_BUILD_BENCHMARKS=ON`
- `BSRVCORE_BUILD_TESTS=ON`
- `BSRVCORE_BUILD_EXAMPLES=ON`
- `BSRVCORE_BENCHMARK_BUILD_BUNDLED_WRK=ON`

The library is built with `-O3` for `Release` in the current CMake
configuration.

## Methodology

### General Method

All benchmarks in this report were run:

- on the same host as the server
- over loopback (`127.0.0.1`)
- with the repository benchmark runner
- with `Release` binaries
- with no concurrent benchmark sweeps running in parallel
- with two repetitions per measured cell

### Measurement Window

Unless otherwise noted, each cell used:

- `warmup_ms=300`
- `duration_ms=1500`
- `cooldown_ms=200`
- `repetitions=2`

### Focus

The main tuning sweeps use `http_get_static` because it is the cleanest probe
for runtime overhead and scheduler effects. A separate all-scenarios run was
used to validate behavior beyond the static GET hot path.

### Important Interpretation Rule

This benchmark is a **throughput benchmark on loopback**. Numbers here should
be interpreted as:

- comparative implementation and runtime-configuration signals
- upper-bound local transport and runtime execution behavior

They should **not** be interpreted as:

- WAN throughput
- TLS throughput
- application-level business logic throughput

## Historical Baseline Used For Comparison

Earlier in the same investigation, the previous worker-pool lifecycle model
produced the following best observed `http_get_static` result:

- **124,145.71 RPS**
- configuration:
  `server_io_threads=10`, `server_worker_threads=20`,
  `client_concurrency=160`, `client_processes=4`,
  `wrk_threads_per_process=2`

Earlier out-of-the-box `saturated` behavior under the previous scheme was:

- **122,978.21 RPS**
- configuration:
  `server_io_threads=10`, `server_worker_threads=20`,
  `client_concurrency=160`, `client_processes=2`,
  `wrk_threads_per_process=1`

These historical numbers are included here because they were measured on the
same machine during this investigation and provide the most relevant local
baseline for the final dispatch-centric runtime.

## Current Final Variant: Default Saturated Result

To estimate out-of-the-box behavior under the final dispatch-centric runtime,
the benchmark was re-run with the built-in `saturated` preset:

| Scenario | Pressure | IO Threads | Worker Threads | Client Concurrency | Client Proc x Threads | Median RPS | p95 (us) | Stability |
|---|---|---:|---:|---:|---|---:|---:|---|
| `http_get_static` | `saturated` | 10 | 20 | 160 | `2 x 1` | 168,556.93 | 1,060.28 | stable |

Interpretation:

- the final runtime already improves the old default saturated result by
  roughly **37.07%**
- however, the preset is **not** optimal for this new runtime model on this
  host

## Parameter Sweep 1: I/O Thread Count

Fixed settings:

- scenario: `http_get_static`
- `server_worker_threads=20`
- `client_concurrency=160`
- `client_processes=4`
- `wrk_threads_per_process=2`

| IO Threads | Median RPS | p95 (us) | Stability |
|---:|---:|---:|---|
| 6 | 180,116.43 | 1,006.92 | stable |
| 8 | 172,121.67 | 1,057.20 | stable |
| 10 | 171,871.90 | 1,046.61 | stable |
| 12 | 168,880.71 | 1,092.91 | stable |

Findings:

- the best result in this sweep is at **6 I/O threads**
- throughput drops as I/O threads rise beyond the local optimum
- this is the opposite of the old mental model where “more I/O threads” looked
  safer for short handlers
- the dispatch-centric runtime is sensitive to strand/cache locality and does
  not want an overly wide I/O fan-out on this machine

## Parameter Sweep 2: Worker Thread Count

Fixed settings:

- scenario: `http_get_static`
- `server_io_threads=6`
- `client_concurrency=160`
- `client_processes=4`
- `wrk_threads_per_process=2`

| Worker Threads | Median RPS | p95 (us) | Stability |
|---:|---:|---:|---|
| 10 | 176,447.38 | 1,027.62 | stable |
| 20 | 174,474.76 | 1,051.41 | stable |
| 30 | 176,908.57 | 1,029.65 | stable |
| 40 | 174,365.00 | 1,075.62 | stable |

Findings:

- the worker-thread count is **much less critical** than before
- the curve is fairly flat between 10 and 30 worker threads
- 30 is slightly best in this sweep, but the advantage is small
- this is consistent with the design goal: the worker pool is no longer on the
  critical path for short I/O-bound handlers

Recommendation:

- keep worker threads moderate
- do not overfit worker-thread count unless the application explicitly uses
  public `Post()` for substantial offloaded work

## Parameter Sweep 3: Client Concurrency

Fixed settings:

- scenario: `http_get_static`
- `server_io_threads=6`
- `server_worker_threads=30`
- `client_processes=4`
- `wrk_threads_per_process=2`

| Client Concurrency | Median RPS | p95 (us) | Stability |
|---:|---:|---:|---|
| 80 | 177,426.43 | 511.45 | stable |
| 160 | 177,200.24 | 1,030.58 | stable |
| 240 | 176,175.00 | 1,541.84 | stable |
| 320 | 175,237.76 | 2,061.24 | stable |

Findings:

- throughput is effectively on a plateau from `80` through `320`
- latency continues to rise as concurrency rises
- on this host, pushing concurrency above `80-160` buys almost no additional
  throughput but exacts a clear tail-latency penalty

Recommendation:

- if the goal is **maximum throughput headline number**, `160` is acceptable
- if the goal is **better tail latency at nearly identical throughput**,
  `80` is the better choice

## Parameter Sweep 4: Load Generator Topology

Fixed settings:

- scenario: `http_get_static`
- `server_io_threads=6`
- `server_worker_threads=30`
- `client_concurrency=160`

| Client Processes | wrk Threads / Process | Median RPS | p95 (us) | Stability |
|---:|---:|---:|---:|---|
| 2 | 1 | 182,293.58 | 967.22 | stable |
| 4 | 1 | 184,870.30 | 966.80 | stable |
| 4 | 2 | 179,082.38 | 1,011.21 | stable |
| 8 | 2 | 176,179.52 | 1,029.97 | stable |

Findings:

- the best observed result in the full report is **4 processes x 1 thread**
- making the load generator “heavier” by increasing per-process wrk threads or
  process count does **not** help here
- once the server is fast enough, the shape of the load generator itself
  becomes a measurable variable

Recommendation:

- for this host and this benchmark, use **`client_processes=4`** and
  **`wrk_threads_per_process=1`**
- do not assume that higher wrk thread counts are more realistic or more fair
  for headline throughput numbers

## Candidate Peak and Balanced Configurations

### Best Observed Peak

| Profile | IO Threads | Worker Threads | Client Concurrency | Client Proc x Threads | Median RPS | p95 (us) |
|---|---:|---:|---:|---|---:|---:|
| Peak observed | 6 | 30 | 160 | `4 x 1` | **184,870.30** | 966.80 |

### Best Balanced Low-Latency Config

| Profile | IO Threads | Worker Threads | Client Concurrency | Client Proc x Threads | Median RPS | p95 (us) |
|---|---:|---:|---:|---|---:|---:|
| Balanced | 6 | 30 | 80 | `4 x 1` | **178,036.04** | 510.99 |

Interpretation:

- the balanced configuration gives up only about **3.69%** throughput versus
  the peak observed configuration
- in return, it cuts `p95` latency by almost half

## Cross-Scenario Results At Recommended Tuned Setting

Configuration:

- `server_io_threads=6`
- `server_worker_threads=30`
- `client_concurrency=160`
- `client_processes=4`
- `wrk_threads_per_process=1`
- `warmup_ms=300`
- `duration_ms=1500`
- `cooldown_ms=200`
- `repetitions=2`

Raw artifact:

- `docs/benchmark-results/2026-03-25-dispatch-all-scenarios.json`

Summary:

| Scenario | Median RPS | p95 (us) | Stability | Notes |
|---|---:|---:|---|---|
| `http_get_static` | 179,941.27 | 1,004.88 | stable | best simple hot path |
| `http_get_route_param` | 175,655.00 | 1,038.54 | stable | small route parsing overhead |
| `http_get_global_aspect` | 178,048.10 | 1,014.79 | stable | single global aspect is cheap here |
| `http_get_aspect_chain_64` | 161,751.90 | 1,133.67 | stable | visible but manageable overhead |
| `http_post_echo_1k` | 84,235.58 | 1,989.53 | stable | body handling dominates |
| `http_post_echo_64k` | 84,838.51 | 1,997.99 | stable | loopback bandwidth is extremely high |
| `http_session_counter` | 169,097.43 | 11,022.48 | stable | throughput high, p95 much worse |

Interpretation:

- routing overhead is now relatively small:
  `http_get_route_param` is only modestly below `http_get_static`
- a single global aspect is almost free in this runtime configuration
- even a 64-aspect chain remains surprisingly strong, though it is clearly
  slower than the simpler GET paths
- POST scenarios remain bandwidth/body dominated rather than dispatch dominated
- session-heavy paths preserve strong throughput but still show severe tail
  latency inflation, pointing to session-map contention and/or session-touch
  overhead as a next optimization target

## Comparison Against The Previous Worker-Pool Lifecycle Model

### Headline Comparison

| Metric | Previous Scheme | Final Dispatch-Centric Scheme | Delta |
|---|---:|---:|---:|
| Best observed `http_get_static` | 124,145.71 | 184,870.30 | **+48.90%** |
| Default `saturated` `http_get_static` | 122,978.21 | 168,556.93 | **+37.06%** |

### What Changed In Practice

The new scheme wins because it removes avoidable pool hops from the fast path
for short handlers and keeps the request lifecycle near the connection strand.
The main improvement is not “more threads”; it is **less scheduler churn in
the fast path** and better locality.

The new measurements also show that the old benchmark preset assumptions are
now stale:

- the best I/O thread count is lower than before
- the worker pool matters less for short handlers
- wrk topology now measurably influences the headline number

## Distance To The 200k Goal

Best observed result in this report:

- `184,870.30 RPS`

Distance to target:

- `200,000 - 184,870.30 = 15,129.70 RPS`
- remaining gap: **7.56%**

Assessment:

- the 200k goal is no longer unrealistic
- however, it is **not** reached yet under the tested parameter matrix
- the remaining gap is now small enough that another focused hot-path pass may
  be sufficient

## Recommendations

### Runtime / Benchmark Presets

1. Retune the benchmark presets for the dispatch-centric runtime model.
   On this machine, `saturated` should not default to `server_io_threads=10`
   if the target workload is short I/O-bound request handling.
2. Add an explicit benchmark preset for this host class:
   `server_io_threads=6`, `server_worker_threads=30`,
   `client_concurrency=160`, `client_processes=4`,
   `wrk_threads_per_process=1`.
3. Add a lower-latency publication preset:
   `server_io_threads=6`, `server_worker_threads=30`,
   `client_concurrency=80`, `client_processes=4`,
   `wrk_threads_per_process=1`.

### Implementation Follow-Ups

1. Revisit session-map contention. The `http_session_counter` path still shows
   a large `p95` penalty relative to the simple GET paths.
2. Revisit route and task-object allocation costs if the 200k target remains
   the next milestone.
3. Consider codifying the dispatch-centric short-handler behavior as the
   intended default runtime model for I/O-heavy workloads.

### Reporting / Documentation

1. Update `docs/manual/benchmarking.md` so the documentation distinguishes:
   - out-of-the-box preset numbers
   - tuned peak numbers
   - low-latency tuned numbers
2. Keep the all-scenarios JSON artifact alongside this report for future
   regression comparison.

## Risks and Limitations

- These results are for loopback HTTP only.
- Scheduler-related host changes made outside the repo are part of the test
  environment but not fully captured as code.
- No full unit/stress test sweep was re-run after the final runtime change;
  this report validates benchmark behavior, not full functional regression.
- The worker-pool historical baseline in this report comes from measurements
  collected earlier in the same investigation, not from a committed JSON
  artifact in this directory.

## Final Conclusion

The dispatch-centric runtime variant is the correct direction for this host and
this workload class.

It raises the local `http_get_static` ceiling from roughly **124k** to roughly
**185k** RPS, with a clean stable result and without changing the public
posting API semantics. The best tuned configuration is now close enough to the
200k target that another targeted optimization pass is justified.

If the immediate next milestone is “ship a stronger benchmark story,” this
runtime variant is ready. If the immediate next milestone is “cross 200k,” the
remaining work should focus on the next hot spots rather than reverting this
runtime design.
