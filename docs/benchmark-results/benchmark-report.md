# Bsrvcore Main-Branch HTTP Benchmark Report

## 1. Current Outcome

This report benchmarks the `main` branch after discarding the `switch-executor` experiment. The tested HTTP scenario is `http_get_static` under single-host self-loop load.

Complete sweep-fix package:

- command: `bash scripts/benchmark.sh run --build-dir build-bench --scenario http_get_static --sweep-depth quick --output-dir .artifacts/benchmark-results/local-main-sweepfix-20260406-054218Z`
- winner: `io15-worker1-conc88-proc3-wrk2`
- `mean_rps = 477092.21`
- `p95 = 2492.46 us`, `p99 = 4119.96 us`
- stability: `stable`
- cell count: `306`

The previous main-branch quick package, before fixing the sweep shape, selected `io10-worker20-conc72-proc3-wrk2` at `452264.99 rps`, `p95 = 414.35 us`, `p99 = 521.25 us`. The new sweep improves peak throughput by `+5.49%`, but the selected winner pays a large tail-latency cost.

![Capacity Overview](./benchmark-report-capacity-overview.png)

## 2. Worker And IO Finding

The updated sweep confirms that the ordinary main-branch HTTP hot path is not worker-bound. `worker=1` can win on throughput when paired with more IO threads and a heavier load-generator shape, but it is not a free improvement.

Top stable cells from the complete sweep-fix package:

| rank | pressure | mean_rps | p95_us | p99_us |
| ---: | --- | ---: | ---: | ---: |
| 1 | `io15-worker1-conc88-proc3-wrk2` | 477092.21 | 2492.46 | 4119.96 |
| 2 | `io15-worker1-conc80-proc4-wrk2` | 475384.56 | 2647.37 | 3912.57 |
| 3 | `io15-worker1-conc80-proc3-wrk2` | 468269.05 | 2110.36 | 3488.80 |
| 4 | `io20-worker1-conc80-proc3-wrk2` | 467945.51 | 2243.86 | 3678.23 |
| 5 | `io20-worker1-conc88-proc3-wrk2` | 467697.62 | 2497.80 | 4058.90 |
| 6 | `io15-worker1-conc88-proc4-wrk2` | 466766.19 | 2319.22 | 3523.67 |
| 7 | `io20-worker1-conc88-proc4-wrk2` | 466449.52 | 2771.41 | 3959.97 |
| 8 | `io24-worker1-conc88-proc3-wrk2` | 466206.91 | 2636.89 | 4166.73 |
| 9 | `io15-worker1-conc72-proc3-wrk2` | 464276.91 | 2129.34 | 3551.26 |
| 10 | `io12-worker1-conc88-proc4-wrk2` | 463913.41 | 1127.97 | 1754.27 |

The practical low-latency counterpoint is that `io10-worker8-conc80-proc4-wrk2` reached `459234.76 rps` with `p95 = 532.72 us` and `p99 = 697.64 us`. That is about `3.74%` below the throughput winner, but with much better tail latency.

Best stable point by IO thread count:

| io_threads | best pressure | mean_rps | p95_us | p99_us |
| ---: | --- | ---: | ---: | ---: |
| 1 | `io1-worker1-conc8-proc2-wrk1` | 99034.85 | 139.10 | 185.38 |
| 5 | `io5-worker10-conc20-proc2-wrk1` | 260299.25 | 217.19 | 264.18 |
| 8 | `io8-worker1-conc80-proc2-wrk1` | 347656.50 | 416.05 | 488.00 |
| 9 | `io9-worker2-conc80-proc2-wrk1` | 356974.00 | 365.44 | 433.40 |
| 10 | `io10-worker8-conc80-proc4-wrk2` | 459234.76 | 532.72 | 697.64 |
| 11 | `io11-worker3-conc80-proc2-wrk1` | 343808.50 | 331.83 | 397.51 |
| 12 | `io12-worker1-conc88-proc4-wrk2` | 463913.41 | 1127.97 | 1754.27 |
| 15 | `io15-worker1-conc88-proc3-wrk2` | 477092.21 | 2492.46 | 4119.96 |
| 20 | `io20-worker1-conc80-proc3-wrk2` | 467945.51 | 2243.86 | 3678.23 |
| 24 | `io24-worker1-conc88-proc3-wrk2` | 466206.91 | 2636.89 | 4166.73 |

Conclusion: `worker=1` is better for this run's maximum RPS only when IO is raised to around `15` and the load generator uses `proc=3/4, wrk=2`. Pushing IO to `20` or `24` does not improve the winner; it keeps throughput in the same class while retaining high tail latency.

![Thread Sensitivity](./benchmark-report-thread-sensitivity.png)

## 3. Sweep Code Update

The benchmark sweep was updated because the old quick matrix under-sampled the current main-branch shape:

- `scripts/benchmark.sh` now seeds low-worker high-IO server pairs in quick mode.
- `scripts/benchmark.sh` now derives coarse concurrency from `max(io_threads, worker_threads)` rather than only `worker_threads`.
- `scripts/benchmark_refine.py` now adds low-worker candidates such as `1/2/4/8`.
- `scripts/benchmark_refine.py` now adds high-IO/worker1 candidates and expands nearby loadgen shapes across `proc=1..4`, `wrk=1..2`, and `concurrency-8/base/+8`.

The completed run used `55` coarse cells and `251` fine cells. This keeps the benchmark compatible with both designs: worker-heavy routes still get sampled through existing worker-centered pairs, and main-branch ordinary HTTP routes now get sampled through IO-heavy / low-worker pairs.

![Peak Neighborhood](./benchmark-report-peak-neighborhood.png)

![Load Generator Sensitivity](./benchmark-report-loadgen-sensitivity.png)

## 4. Environment

Environment snapshot collected with `fastfetch 2.59.0`, `lscpu`, `hostnamectl`, `free`, and compiler/toolchain commands:

- host: `haomingbai-PC`
- hardware: `Lenovo ThinkBook 16p G4 IRH`
- OS: `Fedora Linux 43 (Workstation Edition)`
- kernel: `Linux 6.19.8-200.fc43.x86_64`
- CPU: `13th Gen Intel(R) Core(TM) i9-13900H`
- CPU topology: `1 socket / 14 cores / 20 logical CPUs / 2 threads per core`
- CPU frequency range from `lscpu`: `400 MHz - 5400 MHz`
- CPU frequency policy from `cpupower`: `400 MHz - 4.10 GHz`, governor `powersave`, EPP `balance_performance`, boost active
- memory: `31Gi total`
- GPU: `NVIDIA GeForce RTX 4060 Max-Q / Mobile`, driver `nvidia 580.126.18`; integrated `Intel Iris Xe Graphics`
- CMake: `3.31.11`
- compiler: `GCC 15.2.1 20260123 (Red Hat 15.2.1-7)`

The official package also stores the benchmark-collected environment snapshot in `package/client-env.json`.

## 5. Artifact Index

- official report data: `benchmark-report.json`
- official summary: `package/summary.md`
- official environment: `package/client-env.json`
- official charts:
  - `benchmark-report-capacity-overview.png`
  - `benchmark-report-peak-neighborhood.png`
  - `benchmark-report-thread-sensitivity.png`
  - `benchmark-report-loadgen-sensitivity.png`
- complete sweep-fix run: `.artifacts/benchmark-results/local-main-sweepfix-20260406-054218Z`
- previous main quick run before sweep fix: `.artifacts/benchmark-results/local-main-20260406-045537Z`
