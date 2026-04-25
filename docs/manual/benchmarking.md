# Benchmarking

This chapter describes the benchmark workflow for `HttpServer`.

## Overview

The benchmark stack is split into three layers:

- `bsrvcore_http_benchmark`: runs benchmark cells and exports JSON
- `scripts/benchmark_plot.py`: reads consolidated JSON and generates images only
- `scripts/benchmark.sh`: orchestrates build, sweep, env detection, packaging,
  and single-host or dual-host execution
- `scripts/benchmark_body_matrix.py`: derives body-sweep matrices from a prior
  consolidated benchmark JSON

The script-generated package is intentionally concise. It records the facts,
tables, and plots for a run. The final performance analysis report should be
written by hand from those artifacts.

## Benchmark Modes

The benchmark binary supports three runtime modes:

- `local`: starts a benchmark server in the benchmark subprocess, then runs `wrk`
- `client`: runs `wrk` against `--server-url` and does not start a local server
- `server`: starts one benchmark scenario as a foreground server process

`server` mode requires one named scenario. It does not accept `io` or `all`
because those selectors map to multiple scenario definitions.

## Single-Host Workflow

Run a local sweep:

```bash
bash scripts/benchmark.sh run
```

Common overrides:

```bash
bash scripts/benchmark.sh run \
  --build-dir build-release \
  --scenario mainline \
  --sweep-depth quick
```

`run` does all of the following:

- builds `bsrvcore_http_benchmark`
- resolves a usable `wrk` binary
- detects or creates a Python environment with `matplotlib`
- sweeps benchmark pressure cells
- writes a formatted JSON report
- generates charts
- writes a concise package summary under a gitignored run directory,
  defaulting to `.artifacts/benchmark-results/<UTC timestamp>/package/`

`mainline` is a selector intended for the coarse/fine winner search. It
currently resolves to `http_get_static` and excludes the parameterized body
matrix scenarios.

## Direct Probe Workflow

Use `probe` when you want one explicit point instead of a sweep:

```bash
bash scripts/benchmark.sh probe \
  --build-dir build-release \
  --scenario body-matrix \
  --http-method post \
  --server-io-threads 20 \
  --server-worker-threads 4 \
  --client-concurrency 32 \
  --client-processes 2 \
  --wrk-threads-per-process 2 \
  --request-body-bytes 32768 \
  --response-body-bytes 0
```

`probe` is useful for initial body-size exploration before you decide the
coarse sweep step size.

## Body Sweep Workflow

Body sweeps are intentionally separate from the main winner search:

```bash
bash scripts/benchmark.sh body-run \
  --build-dir build-release \
  --source-json .artifacts/benchmark-results/<mainline run>/benchmark-report.json \
  --body-phase get-response \
  --neighbor-count 5 \
  --body-size-start 0 \
  --body-size-stop 131072 \
  --body-size-step 8192
```

Supported body phases:

- `get-response`: GET with parameterized response size
- `post-request`: POST with parameterized request size and zero-byte response
- `post-matrix`: POST with parameterized request and response sizes

`body-run` derives the winner neighborhood thread groups from the supplied
consolidated JSON and uses the winner's load-generator shape as the reference,
while automatically shrinking `client_processes` / `wrk_threads_per_process`
for low-concurrency points when needed.

## Dual-Host Workflow

### Manual Split Roles

Start the benchmark server on the service host:

```bash
bash scripts/benchmark.sh server \
  --scenario http_get_static \
  --listen-host 0.0.0.0 \
  --listen-port 18080 \
  --server-io-threads 5 \
  --server-worker-threads 10
```

Run the benchmark client on the load-generator host:

```bash
bash scripts/benchmark.sh client \
  --scenario http_get_static \
  --server-url http://service-host:18080 \
  --server-io-threads 5 \
  --server-worker-threads 10 \
  --sweep-depth quick
```

In manual `client` mode, `--server-io-threads` and
`--server-worker-threads` are required so the exported result set still carries
the server configuration that was actually benchmarked.

### SSH-Orchestrated Split Roles

If passwordless SSH is available, the client host can build and start the
remote benchmark server automatically:

```bash
bash scripts/benchmark.sh ssh-run \
  --scenario http_get_static \
  --ssh-target user@service-host \
  --ssh-remote-root /path/to/bsrvcore \
  --server-host service-host \
  --sweep-depth quick
```

`ssh-run` is implemented as a thin wrapper around the same `server` and
`client` roles.

## Sweep Strategy

The sweep is orchestrated by `scripts/benchmark.sh`, not by hardcoding a large
matrix into the benchmark binary.

Current sweep dimensions:

- `server_io_threads`
- `server_worker_threads`
- `client_concurrency`
- `client_processes`
- `wrk_threads_per_process`

Parameterized body sweeps add:

- `request_body_bytes`
- `response_body_bytes`

Depth presets:

- `quick`
- `standard`
- `full`

The sweep is designed to produce useful capacity curves instead of only testing
one or two saturated points.

## Outputs

By default, each run writes outputs into a new gitignored directory:

```bash
.artifacts/benchmark-results/<UTC timestamp>/
```

This avoids accidental repository churn and prevents overwriting older report
sets.

The per-run output directory contains:

- `benchmark-report.json`
- `benchmark-report.md`
- `benchmark-report-capacity-overview.png`
- `benchmark-report-per-connection-throughput.png`
- `benchmark-report-peak-neighborhood.png`
- `benchmark-report-thread-sensitivity.png` (optional, only when the winner shape has comparable thread series)
- `benchmark-report-loadgen-sensitivity.png`

Single-point probes write only their JSON and environment snapshot. They do not
create the full package/report set.

The concise package is written under:

```bash
.artifacts/benchmark-results/<UTC timestamp>/package/
```

It contains:

- `summary.md`
- collected environment snapshots
- links back to the parent JSON and technical report

You can still set a custom output directory explicitly:

```bash
bash scripts/benchmark.sh run --output-dir /path/to/your/output
```

`benchmark_plot.py` does **not** generate Markdown reports.

## Environment Detection

The orchestration script collects benchmark environment details using common
CLI tools where available, including:

- `nproc`
- `lscpu`
- `uname -a`
- `hostname -I`
- `free -h`
- `c++ --version`
- `cmake --version`
- `python3 --version`

Python plotting environment selection order:

1. active `VIRTUAL_ENV` if it already has `matplotlib`
2. repository-local `${BSRVCORE_BENCH_VENV:-.venv-benchmark}`
3. auto-create that local venv and install `matplotlib`
4. system `python3` if it already has `matplotlib`

If none of these work, the script fails explicitly.

## Notes

- Use `Release` builds for comparable throughput numbers.
- Avoid concurrent benchmark sweeps on the same machine.
- The complete performance analysis should be written manually from the
  generated JSON and images, especially when you need to explain saturation,
  decline regions, or bottleneck causes.
