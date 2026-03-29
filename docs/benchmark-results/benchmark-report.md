# Bsrvcore HTTP Benchmark Technical Report

## 1. Scope And Method

This run targets the `http_get_static` path only. The goal is not to claim an exact machine-limit number. The goal is to locate a credible near-peak operating region on the current host, then explain how throughput and latency change around that region.

This point matters for interpreting the charts:

- topology: `single-host`
- host: `haomingbai-PC`
- CPU: `20` logical CPUs, `13th Gen Intel(R) Core(TM) i9-13900H`
- OS: `Fedora Linux 43 (Workstation Edition)`
- kernel: `Linux 6.19.8-200.fc43.x86_64`
- command: `bash scripts/benchmark.sh run --build-dir build-bench --scenario http_get_static --sweep-depth standard`
- warmup / duration / cooldown: `1000 ms / 3000 ms / 500 ms`
- repetitions: `2`
- search shape: `58` coarse cells + `88` fine cells = `146` total cells
- winner rule: stable-first, then highest `mean_rps`, then lower `p95`, then lower `p99`
- reporting rule in this document: use the exact top cell as a reference, but treat all stable cells within `1%` of the top as one near-peak band

Because both server and load generator run on the same machine, the absolute top number is a conservative single-host ceiling, not a pure server-only upper bound. Small differences between adjacent cells can be distorted by local scheduling noise, wrk-side CPU usage, and loopback stack contention. For that reason, the main conclusion below is a near-peak interval, not a single sacred point.

The formatted raw data is in [benchmark-report.json](benchmark-report.json).

## 2. Executive Summary

The exact top stable cell in this run is:

| scenario | io_threads | worker_threads | concurrency | client_processes | wrk_threads | mean_rps | p95_us | p99_us |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `http_get_static` | `5` | `10` | `20` | `2` | `1` | `140095.48` | `167.26` | `181.28` |

For practical use, the more important result is the near-peak band. Stable cells within `1%` of the winner are:

| pressure | mean_rps | p95_us | gap_to_top |
| --- | --- | --- | --- |
| `io5-worker10-conc20-proc2-wrk1` | `140095.48` | `167.26` | `0.00%` |
| `io5-worker10-conc28-proc1-wrk2` | `139717.50` | `234.83` | `0.27%` |
| `io5-worker10-conc36-proc2-wrk1` | `139675.33` | `309.86` | `0.30%` |
| `io5-worker15-conc20-proc2-wrk1` | `139324.00` | `168.22` | `0.55%` |
| `io5-worker12-conc20-proc2-wrk1` | `139137.47` | `167.47` | `0.68%` |
| `io5-worker13-conc20-proc2-wrk1` | `139037.90` | `169.73` | `0.75%` |
| `io5-worker10-conc30-proc2-wrk1` | `139037.50` | `250.00` | `0.76%` |
| `io5-worker10-conc24-proc2-wrk1` | `138965.83` | `203.64` | `0.81%` |
| `io5-worker8-conc20-proc2-wrk1` | `138872.26` | `170.22` | `0.87%` |
| `io5-worker10-conc14-proc2-wrk1` | `138849.35` | `122.53` | `0.89%` |

That band is tighter than the older quick snapshot and is centered lower on the client-pressure axis. The most defensible recommendation from this run is:

- server near-peak center: `io_threads=5`
- useful worker band at that center: `worker_threads=8-15`, with the exact best point at `worker=10`
- client near-peak band on this host: `concurrency=14-36`
- best client shapes in that band: mainly `proc=2,wrk=1`, with one strong alternate `proc=1,wrk=2` point at `conc=28`
- if lower tail latency matters more than the last `0.3%-0.9%` of throughput, prefer `concurrency≈14-24` over `28-36`

## 3. Capacity Overview

The first chart compares representative capacity curves. It is intentionally not "all cells at once". The purpose is to show the baseline, the winner family, a same-server comparison family, and a higher-thread comparison family on the same axes.

![Capacity Overview](./benchmark-report-capacity-overview.png)

The main takeaways are:

- The single-thread baseline tops out at `71983.07 rps` (`io1-worker1-conc8-proc4-wrk1`). The top cell is `94.62%` higher than that baseline, so the gain is real server scaling, not just wrk-side reshaping.
- The winner family `io5-worker10-proc2-wrk1` forms a broad plateau around `138k-140k rps` from `conc=14` through `36`, while the same-server alternate family `io5-worker10-proc1-wrk2` reaches almost the same ceiling only after pushing concurrency higher.
- The chart's higher-thread comparison family `io10-worker20-proc4-wrk2` peaks at `119130.65 rps` (`conc=40`), which is `14.96%` below the top cell. By `conc=160`, its p95 has already stretched to `7274.04 us`.
- The even heavier server pair `io10-worker40` does not recover the lost ground. Its best `proc=2,wrk=1` point reaches only `118654.75 rps`, still `15.30%` below the winner.

So the broad conclusion from this chart is unchanged in direction but sharper in magnitude: the useful region is not at the largest server thread counts. On this host, over-threading costs about fifteen percent of throughput and burns much more latency budget.

## 4. Peak Neighborhood

The next chart zooms into the near-peak region. In this run the exact winner already belongs to the densest near-peak family, `io5-worker10-proc2-wrk1`, so the reference curve and the highlighted top point are part of the same series.

![Peak Neighborhood](./benchmark-report-peak-neighborhood.png)

This curve shows a real near-peak interval, not a single spike:

- `conc=14` reaches `138849.35 rps` at `122.53 us` p95
- `conc=20` reaches the exact top at `140095.48 rps`
- `conc=24` reaches `138965.83 rps`
- `conc=30` reaches `139037.50 rps`
- `conc=36` reaches `139675.33 rps`

Those points are all within `0.30%-0.89%` of the top cell. That is exactly the kind of spread that should be treated as one near-peak band under a single-host benchmark method.

Latency explains why the report should still describe a band, not a single number:

- `conc=14-20` keeps p95 in the `123-167 us` range
- `conc=24-30` stays near peak in throughput, but p95 rises into roughly `204-250 us`
- `conc=36` is still only `0.30%` below the winner, but p95 is already `309.86 us`
- by `conc=44-52`, throughput no longer improves meaningfully while p95 climbs to `359-432 us`

So the practical crest is:

- low-latency crest: `conc≈14-20`
- balanced crest: `conc≈20-24`
- raw-throughput crest: `conc≈28-36`

## 5. Server Thread Sensitivity

The next chart fixes the client shape at `conc=20, proc=2, wrk=1` and varies server thread counts. This is the cleanest way to see whether the winner is a one-off.

![Thread Sensitivity](./benchmark-report-thread-sensitivity.png)

The answer is no. The winner is real, but the plateau around it is broader than a single worker count:

- `io5-worker10-conc20-proc2-wrk1`: `140095.48 rps`, `167.26 us` p95
- `io5-worker8-conc20-proc2-wrk1`: `138872.26 rps`, `170.22 us` p95, only `0.87%` lower
- `io5-worker12-conc20-proc2-wrk1`: `139137.47 rps`, `167.47 us` p95, only `0.68%` lower
- `io5-worker15-conc20-proc2-wrk1`: `139324.00 rps`, `168.22 us` p95, only `0.55%` lower

There is also an important non-monotonic detail: `worker=11` dips to `135792.03 rps`, then `12-15` climbs back near the crest. That is the signature of a same-host thread scheduling tradeoff, not a simple "more workers is better" slope.

That gives a robust server-side conclusion:

- `io=5` is the clear center in this run
- the useful worker region at that center is broad, roughly `8-15`
- moving `io` down to `4` or up to `6` does not collapse throughput, but it does cost several percent and removes the cleanest plateau

For example:

- `io4-worker12-conc20-proc2-wrk1`: `135293.83 rps`, `173.89 us` p95, `3.43%` below the winner
- `io6-worker10-conc20-proc2-wrk1`: `134953.34 rps`, `177.60 us` p95, `3.67%` below the winner

## 6. Load Generator Sensitivity

Because this is a single-host run, client-side shape matters. The next chart fixes the server at `io=5, worker=10` and compares several `proc/wrk` families across concurrency.

![Loadgen Sensitivity](./benchmark-report-loadgen-sensitivity.png)

This chart is the strongest evidence that the benchmark method itself materially changes the measured ceiling:

- `proc=2,wrk=1` reaches the exact winner at `conc=20`: `140095.48 rps`, `167.26 us` p95
- `proc=1,wrk=2` reaches `139717.50 rps` at `conc=28`, only `0.27%` below the top, but with higher p95 at `234.83 us`
- `proc=4,wrk=1` peaks at `136377.90 rps` at `conc=28`, already `2.65%` below the top
- `proc=2,wrk=2` peaks at `135276.93 rps` at `conc=36`, `3.44%` below the top
- the naive `proc=1,wrk=1` family never becomes competitive here; its best point is only `115401.00 rps` at `conc=40`, which is `17.63%` below the winner and unstable

This is exactly what a same-host benchmark should look like:

- server threads, client threads, and loopback sockets all compete on the same CPU pool
- a different wrk fan-out can move the apparent ceiling by double-digit percentages
- small deltas inside the best families are still useful for finding a band, but the ceiling is a property of the whole arrangement, not only the server

## 7. What This Means For Single-Host Benchmarking

This run is good enough to locate the near-peak region, but it should not be used to overstate precision.

The disciplined way to read it is:

- The single-host benchmark is valid for relative tuning.
  It clearly separates the baseline, the near-peak server thread region, and the over-threaded region.
- The single-host benchmark is not a pure server ceiling.
  The best points depend strongly on the wrk process/thread layout, which means the measurement includes wrk-side CPU and loopback overhead.
- The right output is a band, not a point.
  On this run the credible near-peak band is centered on `io=5`, `worker=8-15`, `concurrency=14-36`, mainly with `proc=2,wrk=1` and one strong alternate `proc=1,wrk=2` shape.

If the next step is to estimate server-only headroom, the correct follow-up is still a dual-host run with the server fixed near this band and only the client side swept from a second machine.

## 8. Recommended Configurations

For this machine and this benchmark path:

- Best single-host point from this run:
  `io=5, worker=10, conc=20, proc=2, wrk=1`
- Best low-latency near-peak point:
  `io=5, worker=10, conc=14, proc=2, wrk=1`
  It is only `0.89%` below the top, but p95 drops to `122.53 us`.
- Best high-throughput near-peak point if you accept more queueing:
  `io=5, worker=10, conc=28, proc=1, wrk=2`
  It is only `0.27%` below the top, but p95 rises to `234.83 us`.
- Best simpler high-throughput point if you want to keep the same `proc=2,wrk=1` client shape as the winner family:
  `io=5, worker=10, conc=36, proc=2, wrk=1`
  It is only `0.30%` below the top, but p95 rises further to `309.86 us`.

## 9. Artifacts

- Report: [benchmark-report.md](benchmark-report.md)
- Data: [benchmark-report.json](benchmark-report.json)
- Concise package: [package/summary.md](./package/summary.md)
