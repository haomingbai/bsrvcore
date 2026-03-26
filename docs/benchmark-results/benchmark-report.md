# Bsrvcore HTTP Benchmark Technical Report

## 1. Scope And Method

This run targets the `http_get_static` path only. The goal is not to claim an exact machine-limit number. The goal is to locate a credible near-peak operating region on the current host, then explain how throughput and latency change around that region.

This point matters for interpreting the charts:

- topology: `single-host`
- host: `haomingbai-PC`
- CPU: `20` logical CPUs, `13th Gen Intel(R) Core(TM) i9-13900H`
- OS: `Fedora Linux 43 (Workstation Edition)`
- kernel: `Linux 6.19.8-200.fc43.x86_64`
- command: `bash scripts/benchmark.sh run --build-dir build-release --scenario http_get_static --sweep-depth quick`
- warmup / duration / cooldown: `800 ms / 2000 ms / 400 ms`
- repetitions: `2`
- search shape: `23` coarse cells + `83` fine cells = `106` total cells
- winner rule: stable-first, then highest `mean_rps`, then lower `p95`, then lower `p99`
- reporting rule in this document: use the exact top cell as a reference, but treat all stable cells within `1%` of the top as one near-peak band

Because both server and load generator run on the same machine, the absolute top number is a conservative single-host ceiling, not a pure server-only upper bound. Small differences between adjacent cells can be distorted by local scheduling noise, wrk-side CPU usage, and loopback stack contention. For that reason, the main conclusion below is a near-peak interval, not a single sacred point.

The formatted raw data is in [benchmark-report.json](benchmark-report.json).

## 2. Executive Summary

The exact top stable cell in this run is:

| scenario | io_threads | worker_threads | concurrency | client_processes | wrk_threads | mean_rps | p95_us | p99_us |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `http_get_static` | `5` | `11` | `40` | `2` | `1` | `184256.43` | `244.88` | `259.99` |

For practical use, the more important result is the near-peak band. Stable cells within `1%` of the winner are:

| pressure | mean_rps | p95_us | gap_to_top |
| --- | --- | --- | --- |
| `io5-worker11-conc40-proc2-wrk1` | `184256.43` | `244.88` | `0.00%` |
| `io5-worker10-conc56-proc1-wrk2` | `183682.24` | `341.44` | `0.31%` |
| `io5-worker10-conc40-proc1-wrk2` | `183445.24` | `244.83` | `0.44%` |
| `io5-worker10-conc60-proc2-wrk1` | `182486.30` | `373.73` | `0.96%` |
| `io6-worker11-conc40-proc2-wrk1` | `182484.93` | `250.17` | `0.96%` |

That band is narrow enough to be meaningful but wide enough that it should not be overfit. The most defensible recommendation from this run is:

- server near-peak range: `io_threads=5-6`, `worker_threads=10-11`
- client near-peak range on this host: `concurrency=40-60`, with `proc=1,wrk=2` or `proc=2,wrk=1`
- if lower tail latency matters more than the last `0.5%-1%` of throughput, prefer the `concurrency≈40` points over the `56-60` points

## 3. Capacity Overview

The first chart compares representative capacity curves. It is intentionally not “all cells at once”. The purpose is to show the baseline, the near-peak family, and an over-threaded comparison family on the same axes.

![Capacity Overview](./benchmark-report-capacity-overview.png)

The main takeaways are:

- The single-thread baseline tops out at `99401.37 rps` (`io1-worker1-conc8-proc1-wrk1`). The top cell is `85.37%` higher than that baseline, so the gain is real server scaling, not just wrk-side tuning.
- The over-threaded family `io10-worker20` never catches up. `io10-worker20-conc40-proc2-wrk1` is `7.56%` below the top cell, and `io10-worker20-conc80-proc2-wrk1` is `8.61%` below while also pushing p95 much higher.
- The broad near-peak family is centered on `io5-worker10/11`, not on the largest thread counts. On this host, more server threads are not safer; they are already on the wrong side of the scheduler/cache tradeoff.

## 4. Peak Neighborhood

The next chart zooms into the near-peak region. The curve itself uses the densest near-peak family, `io5-worker10-proc2-wrk1`, because it has a full concurrency sweep from `10` to `80`. The exact winner cell `io5-worker11-conc40-proc2-wrk1` is overlaid as the highlighted top point.

This is deliberate. The exact winner is a fine-grained thread-tuning point, not a full family sweep. Plotting the dense nearby family is more honest than pretending a single-point winner defines a whole curve.

![Peak Neighborhood](./benchmark-report-peak-neighborhood.png)

This curve shows a real near-peak interval, not a single spike:

- `conc=38` on the reference family reaches `182334.57 rps`
- `conc=40` reaches `182202.62 rps`
- `conc=46` reaches `181731.00 rps`
- `conc=60` reaches `182486.30 rps`

These points are all within about `0.3%-1.1%` of the top cell once thread tuning is accounted for. That is exactly the kind of spread that should be treated as one near-peak band under a single-host benchmark method.

Latency explains why this should be described as a band, not just “higher concurrency is better”:

- `conc=40` around the winner stays near `245-276 us` p95
- `conc=56-60` is still near-peak in throughput, but p95 rises into roughly `341-374 us`
- beyond that, throughput stops buying enough headroom to justify the extra queueing

So the practical crest is:

- lower-latency crest: `conc≈40`
- raw-throughput crest: `conc≈56-60`

## 5. Server Thread Sensitivity

The next chart fixes the client shape at `conc=40, proc=2, wrk=1` and varies server thread counts. This is the cleanest way to see whether the exact winner is a fluke.

![Thread Sensitivity](./benchmark-report-thread-sensitivity.png)

The answer is no. The winner is not a random outlier, but the “best point” should still be read as a small plateau:

- `io5-worker11-conc40-proc2-wrk1`: `184256.43 rps`, `244.88 us` p95
- `io5-worker10-conc40-proc2-wrk1`: `182202.62 rps`, `275.82 us` p95, only `1.12%` lower
- `io6-worker11-conc40-proc2-wrk1`: `182484.93 rps`, `250.17 us` p95, only `0.96%` lower
- `io5-worker12-conc40-proc2-wrk1`: `180506.52 rps`, `247.64 us` p95, still close but clearly off the crest

That gives a robust server-side conclusion:

- the useful region is `io=5-6`
- the useful worker region is `10-11`
- `worker=11` is the best point in this run, but `10-12` is the real tuning band

Once worker threads move up toward `13-14`, throughput softens again. That is the same “too many threads” pattern already visible in the coarse `io10-worker20` family.

## 6. Load Generator Sensitivity

Because this is a single-host run, client-side shape matters. The next chart uses the densest near-peak server family, `io5-worker10`, and compares several `proc/wrk` shapes across concurrency.

![Loadgen Sensitivity](./benchmark-report-loadgen-sensitivity.png)

This chart is the strongest evidence that the benchmark method itself affects the measured ceiling:

- `io5-worker10-conc40-proc1-wrk2` reaches `183445.24 rps` with `244.83 us` p95, only `0.44%` below the top cell
- `io5-worker10-conc60-proc2-wrk1` reaches `182486.30 rps`, but p95 rises to `373.73 us`
- `proc=3/4` shapes stay close, but they do not beat the best `proc=1/2` shapes
- extra fan-out or extra wrk threads do not create a clean monotonic improvement; they just move the local resource tradeoff around

This is exactly what a same-host benchmark should look like:

- server threads, client threads, and loopback sockets all compete on the same CPU pool
- the highest measured `rps` is therefore a property of the whole test arrangement, not only the server
- small deltas near the top are real enough to locate a useful operating region, but not precise enough to declare one client shape universally correct

## 7. What This Means For Single-Host Benchmarking

This run is good enough to locate the near-peak region, but it should not be used to overstate precision.

The disciplined way to read it is:

- The single-host benchmark is valid for relative tuning.
  It clearly separates the baseline, the near-peak server thread region, and the over-threaded region.
- The single-host benchmark is not a pure server ceiling.
  The best points depend on local client shape, which means the measurement includes wrk-side CPU and loopback overhead.
- The right output is a band, not a point.
  On this run the credible near-peak band is centered on `io=5-6`, `worker=10-11`, `concurrency=40-60`.

If the next step is to estimate server-only headroom, the correct follow-up is a dual-host run with the server fixed near this band, then sweep only the client side from a second machine.

## 8. Recommended Configurations

For this machine and this benchmark path:

- Best single-host point from this run:
  `io=5, worker=11, conc=40, proc=2, wrk=1`
- Best low-latency near-peak point:
  `io=5, worker=10, conc=40, proc=1, wrk=2`
  It is only `0.44%` below the top, with essentially the same p95.
- Best high-throughput near-peak point if you accept more queueing:
  `io=5, worker=10, conc=60, proc=2, wrk=1`
  It is only `0.96%` below the top, but p95 is much higher.

## 9. Artifacts

- Report: [benchmark-report.md](benchmark-report.md)
- Data: [benchmark-report.json](benchmark-report.json)
- Concise package: [package/summary.md](./package/summary.md)
