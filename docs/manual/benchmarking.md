# Benchmarking

This chapter explains how to run the built-in benchmark suite for
`HttpServer`.

## Execution Model

Each benchmark cell (`scenario x pressure x repetition`) runs in a fresh child
process.

Inside one cell process:

- one `HttpServer` instance is started
- `N` independent `wrk` processes pressure that server in parallel
- warmup, measure, and cooldown are all executed with the same multi-process
  model

This keeps the load model closer to real deployment pressure while preserving
subprocess isolation between cells.

## No Repository Binaries By Default

The repository does not ship `wrk` binaries.

Default behavior:

- bundled `wrk` build is `OFF`
- runtime path probing order is:
  `/bin/wrk`, `/usr/bin/wrk`, `/usr/local/bin/wrk`, then `wrk` from `PATH`

You can choose one of these paths:

1. install `wrk` in your system path (or under `/bin`)
2. set `--wrk-bin /absolute/path/to/wrk`
3. set `BSRVCORE_BENCHMARK_WRK_BIN=/absolute/path/to/wrk`
4. enable bundled build explicitly at configure time

Optional bundled build switch:

```bash
-DBSRVCORE_BENCHMARK_BUILD_BUNDLED_WRK=ON
```

If enabled, `wrk` is built under the build directory only.

## Build

Build the benchmark binary:

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

Scenario selectors:

- `--scenario all`: all benchmark scenarios
- `--scenario io`: IO-focused subset
- `--scenario <name>`: one named scenario

Current IO-focused subset:

- `http_get_static`
- `http_post_echo_1k`
- `http_post_echo_64k`

## Pressure Levels

Built-in pressure presets:

- `light`: `server_io_threads=1`, `server_worker_threads=1`,
  `client_concurrency=1`
- `balanced`: `server_io_threads=max(1, floor(cpu/4))`,
  `server_worker_threads=max(1, floor(cpu/2))`,
  `client_concurrency=max(4, server_worker_threads*4)`
- `saturated`: `server_io_threads=max(1, floor(cpu/2))`,
  `server_worker_threads=max(1, cpu)`,
  `client_concurrency=max(16, server_worker_threads*8)`
- `overload`: `server_io_threads=max(1, floor(cpu/2))`,
  `server_worker_threads=max(2, cpu*2)`,
  `client_concurrency=max(32, server_worker_threads*8)`

Custom override:

- `--server-io-threads <n>`
- `--server-worker-threads <n>`
- `--client-concurrency <n>`

Compatibility note:

- `--server-threads <n>` is still accepted and maps to
  `server_io_threads=n` plus `server_worker_threads=n`.

When either override is passed, the sweep collapses to one custom pressure
cell.

## Multi-Process Load Knobs

- `--client-processes <n>`: number of `wrk` processes per phase (default: 2)
- `--wrk-threads-per-process <n>`: `wrk` threads used by each process (default: 1)
- `--wrk-bin <path>`: explicit `wrk` path

Examples:

```bash
./build-bench/benchmarks/bsrvcore_http_benchmark \
  --scenario io \
  --pressure saturated \
  --client-processes 4 \
  --wrk-threads-per-process 2 \
  --wrk-bin /usr/bin/wrk
```

## Output

Optional JSON output:

```bash
./build-bench/benchmarks/bsrvcore_http_benchmark \
  --scenario io \
  --pressure saturated \
  --output-json ./benchmark.json
```

JSON includes:

- environment info
- run config (including `wrk` path and multi-process knobs)
- per-run metrics
- per-cell aggregate metrics

## Helper Scripts

The repository also provides helper scripts under `scripts/`:

- `scripts/build.sh`: configure and build with common defaults
- `scripts/format.sh`: run `clang-format` across the repository
- `scripts/benchmark.sh`: build the benchmark binary, detect CPU count,
  run a benchmark sweep, create a Python venv, and generate plots plus a short
  Markdown report at `docs/benchmark-results/benchmark-report.md`

## Metrics

Per repetition, the benchmark records:

- `attempt_count`
- `success_count`
- `error_count`
- `non_2xx_3xx_count`
- `socket_connect_error_count`
- `socket_read_error_count`
- `socket_write_error_count`
- `socket_timeout_error_count`
- `loadgen_failure_count`
- `attempt_rps`
- `rps`
- `failure_ratio`
- `mib_per_sec`
- latency in microseconds: `p50`, `p95`, `p99`, `max`

Failure metrics are part of the benchmark result by design. They are not
retried away.

## Stability Rule

A cell is `stable` only when all are true:

- `rps_cv <= 10%`
- `p95_cv <= 15%`
- `failure_ratio.max <= 5%`
- `loadgen_failure_count.max == 0`

Otherwise it is `unstable`.

## Reproducibility

For comparable numbers:

- use `Release`
- keep the same scenario and pressure cell
- avoid background CPU/network pressure
- do not run multiple benchmark sweeps at the same time

Set `BSRVCORE_BENCHMARK_TRACE=1` to print lifecycle tracing for debugging.
