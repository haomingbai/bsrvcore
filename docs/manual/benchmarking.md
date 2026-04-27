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

Run a local sweep with defaults:

```bash
bash scripts/benchmark.sh run
```

Common overrides for quick iteration:

```bash
# Fast smoke test with specific concurrency values
bash scripts/benchmark.sh run \
  --scenario mainline \
  --sweep-depth quick \
  --coarse-concurrency-values "8,16,32,64,96,128" \
  --coarse-only

# Standard sweep with custom build directory
bash scripts/benchmark.sh run \
  --build-dir build-bench \
  --scenario mainline \
  --sweep-depth standard
```

`run` does all of the following:

- builds `bsrvcore_http_benchmark` (with bundled wrk)
- resolves a usable `wrk` binary (prefers the bundled one)
- detects or creates a Python environment with required modules
- sweeps benchmark pressure cells (coarse + optional fine refinement)
- writes a consolidated JSON report
- generates charts (capacity overview, per-connection throughput,
  peak neighborhood, thread sensitivity, loadgen sensitivity)
- writes a concise package summary under a gitignored run directory,
  defaulting to `.artifacts/benchmark-results/<UTC timestamp>/`

`mainline` is a selector intended for the coarse/fine winner search. It
currently resolves to `http_get_static` and excludes the parameterized body
matrix scenarios.

Use `--coarse-only` to skip the fine-grained refinement sweep — this is
useful for quick iteration or when you only need a rough capacity curve.
Use `--coarse-concurrency-values` with a comma-separated list to control
exactly which concurrency levels are tested, instead of the default
multiples (1×, 2×, 4×, 8× of the pressure thread count).

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

### Prerequisites

Both hosts must have:

- The bsrvcore source tree at the same (or specified) path
- C++ build toolchain (GCC/Clang, cmake ≥ 3.20)
- Python 3 with venv support
- OpenSSL and Boost development headers

The load-generator host additionally needs passwordless SSH access to the
server host, configured via `~/.ssh/config` or explicit SSH options.

### SSH-Orchestrated Split Roles (recommended)

`ssh-run` is the simplest dual-host mode. It builds the benchmark binary on
both hosts, starts the server on the remote, runs the sweep locally, and
packages results, all in one command.

Use an SSH config alias (preferred):

```bash
# ~/.ssh/config on the load-generator host:
# Host myserver
#   HostName 10.0.0.5
#   User myuser
#   Port 22

bash scripts/benchmark.sh ssh-run \
  --scenario http_get_static \
  --ssh-target myserver \
  --ssh-remote-root /home/myuser/bsrvcore \
  --sweep-depth quick \
  --coarse-concurrency-values "8,16,32,64,128"
```

Or use an explicit `user@host`:

```bash
bash scripts/benchmark.sh ssh-run \
  --scenario http_get_static \
  --ssh-target myuser@10.0.0.5 \
  --ssh-remote-root /home/myuser/bsrvcore \
  --sweep-depth standard
```

Key SSH options:

| Option | Default | Purpose |
|---|---|---|
| `--ssh-target` | (required) | SSH destination (`user@host` or config alias) |
| `--ssh-remote-root` | same as local `$ROOT_DIR` | Path to bsrvcore on the server host |
| `--ssh-port` | (from config) | Override SSH port |
| `--ssh-key` | (from config) | Override SSH identity file |
| `--server-host` | resolved via `ssh -G` | Override the IP/hostname the client uses to reach the server |

`ssh-run` resolves `--server-host` automatically from your SSH config when
using a config alias. It calls `ssh -G <target>` to read the resolved
`hostname` directive. If you override `--server-host`, that value is used
directly.

### Manual Split Roles

When `ssh-run` is not suitable (e.g. the server runs in a container with a
custom entrypoint), you can split the roles manually.

Start the benchmark server on the service host:

```bash
# On the server host:
cd /path/to/bsrvcore
bash scripts/benchmark.sh server \
  --scenario http_get_static \
  --listen-host 0.0.0.0 \
  --listen-port 18080 \
  --server-io-threads 10 \
  --server-worker-threads 20
```

Run the benchmark client on the load-generator host:

```bash
# On the load-generator host:
bash scripts/benchmark.sh client \
  --scenario http_get_static \
  --server-url http://10.0.0.5:18080 \
  --server-io-threads 10 \
  --server-worker-threads 20 \
  --sweep-depth quick
```

In manual `client` mode, `--server-io-threads` and
`--server-worker-threads` are required so the exported result set carries
the real server configuration.

You can also run a single-point probe against a remote server without a
full sweep:

```bash
bash scripts/benchmark.sh probe \
  --scenario http_get_static \
  --mode client \
  --server-url http://10.0.0.5:18080 \
  --server-io-threads 10 \
  --server-worker-threads 20 \
  --client-concurrency 64 \
  --build-dir build-bench
```

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

1. active `VIRTUAL_ENV` if it already has the required modules
2. repository-local `${BSRVCORE_BENCH_VENV:-.artifacts/.venv-benchmark}`
3. auto-create that local venv and install dependencies from
   `scripts/requirements-bench.txt` (matplotlib, numpy, paramiko)
4. system `python3` if it already has the required modules

If none of these work, the script fails explicitly.

The first successful venv creation may take a moment because of the `pip
install` step. Subsequent runs reuse the same venv.

## Troubleshooting

**SSH hangs during `ssh-run`**: This is a known issue with certain
combinations of `cd` and `nohup` over SSH. The benchmark script uses
absolute paths on the remote side to avoid this. If you encounter SSH
hangs with custom scripts, avoid `cd <dir> && nohup ... &` and use
absolute paths instead.

**Port already in use**: If port 18080 is occupied, use `--listen-port` to
pick a different port. The `ssh-run` flow starts a fresh server per
thread-group, so it always needs the configured port to be free on the
remote host.

**`ensure_bench_python` failures**: If the Python venv setup fails, check
that `python3 -m venv` works on your system. On minimal installations you
may need to install `python3-venv` (Debian/Ubuntu) or equivalent.

**Server doesn't respond**: Ensure the remote firewall allows the listen
port. On Ubuntu, check with `sudo ufw status`. The benchmark server
listens on `0.0.0.0` by default.

**Benchmark binary returns HTTP 502 to plain curl**: The benchmark server
expects the specific request patterns that the benchmark client sends via
wrk. It is not a general-purpose HTTP server — use the `probe` or
`client` commands for testing.

## Quick Reference

Common task patterns:

```bash
# Smoke test (fastest: 1 concurrency, 1 cell, local only)
bash scripts/benchmark.sh probe \
  --scenario http_get_static \
  --client-concurrency 8

# Local sweep with explicit concurrency values, skip refinement
bash scripts/benchmark.sh run \
  --scenario mainline \
  --sweep-depth quick \
  --coarse-concurrency-values "8,16,32,64,128" \
  --coarse-only

# Full dual-host sweep via SSH
bash scripts/benchmark.sh ssh-run \
  --scenario http_get_static \
  --ssh-target myserver \
  --ssh-remote-root /home/myuser/bsrvcore \
  --sweep-depth standard \
  --coarse-concurrency-values "8,16,32,64,96,128,192,256"

# Remote server + local client (manual split)
bash scripts/benchmark.sh client \
  --scenario http_get_static \
  --server-url http://10.0.0.5:18080 \
  --server-io-threads 10 \
  --server-worker-threads 20 \
  --sweep-depth standard

# Body size sweep (requires a prior mainline run)
bash scripts/benchmark.sh body-run \
  --source-json .artifacts/benchmark-results/<run>/benchmark-report.json \
  --body-phase post-matrix \
  --body-size-start 0 \
  --body-size-stop 131072 \
  --body-size-step 8192
```

## Notes

- Use `Release` builds for comparable throughput numbers.
- Avoid concurrent benchmark sweeps on the same machine.
- The complete performance analysis should be written manually from the
  generated JSON and images, especially when you need to explain saturation,
  decline regions, or bottleneck causes.
- The script sets `BSRVCORE_BUILD_DIR` to `build-bench` by default (separate
  from the standard `build-release` directory) to keep benchmark binaries
  isolated. Use `--build-dir` to override.
- When using `ssh-run`, the remote source tree path (`--ssh-remote-root`)
  must point to a copy of the same bsrvcore source. The script builds on
  both ends, so it is OK if the remote was not previously compiled.
