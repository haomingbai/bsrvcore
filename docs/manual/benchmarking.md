# Benchmarking

This chapter explains how to run the built-in benchmark suite for `HttpServer`.

The benchmark tree is **independent from `tests/`**:

- no `GTest`
- no `ctest`
- no `tests/support`
- no external tools such as `wrk`, `ab`, or `google-benchmark`

It uses only this repository plus the normal project dependencies.

## Execution Model

Each benchmark cell (`scenario x pressure x repetition`) runs in a fresh child process.

This is intentional:

- it keeps benchmark runs independent from `tests/`
- it prevents cross-cell state from polluting later measurements
- it turns a stuck cell into a normal subprocess failure instead of hanging the whole sweep forever

The benchmark client uses Boost.Beast asynchronous operations with explicit deadlines.
That means request-level timeouts are real I/O deadlines, not blocking calls that wait forever.

## Build

Build the benchmark binary explicitly:

```bash
cmake -S . -B build-bench \
  -DCMAKE_BUILD_TYPE=Release \
  -DBSRVCORE_BUILD_EXAMPLES=OFF \
  -DBSRVCORE_BUILD_TESTS=OFF \
  -DBSRVCORE_BUILD_BENCHMARKS=ON
cmake --build build-bench --target bsrvcore_http_benchmark --parallel
```

Binary path:

```bash
./build-bench/benchmarks/bsrvcore_http_benchmark
```

## Scenarios

List scenarios:

```bash
./build-bench/benchmarks/bsrvcore_http_benchmark --list-scenarios
```

Current scenarios:

- `http_get_static`: `GET /ping`, small text body, baseline request path
- `http_get_route_param`: `GET /users/{id}`, path parameter extraction cost
- `http_get_global_aspect`: static GET + one global pre/post aspect pair
- `http_post_echo_1k`: `POST /echo`, `1 KiB` echo body
- `http_post_echo_64k`: `POST /echo`, `64 KiB` echo body
- `http_session_counter`: cookie + session hot path with a per-session counter

## Pressure Levels

The benchmark resolves four pressure presets from the current machine's logical CPU count:

- `light`: `server_threads=1`, `client_concurrency=1`
- `balanced`: `server_threads=max(1, ceil(cpu/2))`, `client_concurrency=max(4, server_threads*4)`
- `saturated`: `server_threads=max(1, cpu)`, `client_concurrency=max(16, server_threads*8)`
- `overload`: `server_threads=max(1, cpu)`, `client_concurrency=max(32, server_threads*16)`

In this benchmark implementation, `server_threads` is applied to both:

- `HttpServerExecutorOptions::core_thread_num/max_thread_num`
- `HttpServer::Start(thread_count)`

That keeps one public CLI knob even though the current `HttpServer` API splits worker and I/O configuration.

## Profiles

- `quick`: `light + saturated`, `warmup=1000ms`, `duration=3000ms`, `repetitions=2`, `cooldown=500ms`
- `full`: `light + balanced + saturated + overload`, `warmup=2000ms`, `duration=8000ms`, `repetitions=5`, `cooldown=1000ms`

Examples:

```bash
./build-bench/benchmarks/bsrvcore_http_benchmark --profile quick
./build-bench/benchmarks/bsrvcore_http_benchmark --profile full
./build-bench/benchmarks/bsrvcore_http_benchmark --scenario http_post_echo_64k --pressure saturated
./build-bench/benchmarks/bsrvcore_http_benchmark --scenario all --pressure all --warmup-ms 1000 --duration-ms 3000 --repetitions 2 --cooldown-ms 500
```

If you pass `--server-threads` or `--client-concurrency`, the preset sweep collapses into one custom cell.

## Output

Optional JSON output:

```bash
./build-bench/benchmarks/bsrvcore_http_benchmark \
  --profile quick \
  --output-json ./benchmark.json
```

JSON includes:

- environment info: timestamp, OS, compiler, build type, logical CPU count
- run config: scenario/profile/pressure and timing settings
- per-run metrics
- per-cell aggregates
- stability label: `stable` or `unstable`

## Metrics

Each repetition records:

- `success_count`
- `error_count`
- `bytes_sent`
- `bytes_received`
- `rps`
- `mib_per_sec`
- latency percentiles in microseconds: `p50`, `p95`, `p99`, `max`

`mib_per_sec` is based on total wire bytes (`bytes_sent + bytes_received`) divided by measured time.

Headline values use the **median** across repetitions. JSON also includes:

- `mean`
- `min`
- `max`
- `stdev`
- `cv`

Stability rule:

- `rps_cv <= 10%`
- `p95_cv <= 15%`

If both hold, the cell is marked `stable`; otherwise `unstable`.

## Reproducibility

For comparable numbers:

- use `Release`
- avoid background CPU and network load
- do not run multiple benchmark sweeps at the same time
- keep the same compiler and dependency set
- compare the same scenario and pressure cell, not only the headline profile

`BSRVCORE_BENCHMARK_TRACE=1` enables extra lifecycle tracing for debugging stuck or flaky runs.

## Limits

This suite is intentionally narrow:

- it benchmarks only this project
- it does **not** compare against other frameworks
- it uses in-process loopback traffic on one machine
- it does not cover TLS handshake cost, SSE, `bsrvrun`, or connection-storm behavior

So the numbers are useful as a **project-local baseline**, not as a direct prediction of production internet performance.

## Snapshot

The repository snapshot for the current machine lives under:

- `docs/benchmark-results/`

It contains the raw JSON and one summarized Markdown report.
