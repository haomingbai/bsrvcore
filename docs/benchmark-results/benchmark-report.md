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
| `http_get_static` | `6` | `12` | `80` | `2` | `1` | `178563.83` | `501.22` | `529.00` |

For practical use, the more important result is the near-peak band. Stable cells within `1%` of the winner are:

| pressure | mean_rps | p95_us | gap_to_top |
| --- | --- | --- | --- |
| `io6-worker12-conc80-proc2-wrk1` | `178563.83` | `501.22` | `0.00%` |
| `io5-worker10-conc60-proc1-wrk2` | `178442.71` | `371.11` | `0.07%` |
| `io5-worker10-conc108-proc2-wrk1` | `178264.67` | `670.03` | `0.17%` |
| `io6-worker7-conc80-proc2-wrk1` | `178238.09` | `501.07` | `0.18%` |
| `io5-worker10-conc90-proc2-wrk1` | `178219.00` | `559.78` | `0.19%` |
| `io6-worker9-conc80-proc2-wrk1` | `177956.67` | `503.72` | `0.34%` |
| `io5-worker10-conc104-proc2-wrk1` | `177785.67` | `645.81` | `0.44%` |
| `io5-worker10-conc86-proc2-wrk1` | `177750.67` | `531.75` | `0.46%` |
| `io5-worker15-conc80-proc2-wrk1` | `177712.83` | `494.17` | `0.48%` |
| `io6-worker8-conc80-proc2-wrk1` | `177578.83` | `510.33` | `0.55%` |
| `io5-worker9-conc80-proc2-wrk1` | `177561.13` | `492.39` | `0.56%` |
| `io6-worker13-conc80-proc2-wrk1` | `177523.17` | `508.83` | `0.58%` |
| `io5-worker14-conc80-proc2-wrk1` | `177452.09` | `494.23` | `0.62%` |
| `io5-worker10-conc74-proc2-wrk1` | `177242.17` | `464.81` | `0.74%` |

That band is much healthier than the earlier kernel-update outlier run and now matches the restored anchor checks: a broad plateau around `177k-178.5k`, centered mainly on `io=5-6`, with two dominant loadgen shapes.

The most defensible recommendation from this run is:

- server near-peak center: `io_threads=5-6`
- useful worker band at that center: roughly `worker_threads=7-15`
- client near-peak band on this host: mainly `concurrency=60-108`
- best client shapes in that band: `proc=2,wrk=1` across a wide plateau, plus one especially strong alternate `proc=1,wrk=2` point at `conc=60`
- if latency matters more than the last `0.1%-0.7%` of throughput, prefer the lower-latency alternate `proc=1,wrk=2` near `conc=60` instead of pushing `proc=2,wrk=1` into `90+`

## 3. Capacity Overview

The first chart compares representative capacity curves. It is intentionally not "all cells at once". The purpose is to show the baseline, the winner family, a same-server comparison family, and a higher-thread comparison family on the same axes.

![Capacity Overview](./benchmark-report-capacity-overview.png)

The main takeaways are:

- The single-thread baseline tops out at `89770.48 rps` (`io1-worker1-conc8-proc4-wrk1`). The top cell is `98.91%` higher than that baseline, so the gain is real server scaling, not just wrk-side reshaping.
- The dense winner-family reference `io5-worker10-proc2-wrk1` forms a broad high-throughput plateau from roughly `conc=56` through `108`, mostly in the `176k-178k` band.
- The same-server alternate family `io5-worker10-proc1-wrk2` reaches `178442.71 rps` at `conc=60`, only `0.07%` below the exact winner, while keeping p95 much lower at `371.11 us`.
- The chart's higher-thread comparison family `io10-worker20-proc4-wrk2` peaks at `167447.26 rps` (`conc=80`), which is `6.22%` below the top cell. By `conc=160`, its p95 has already stretched to `1087.50 us`.
- The even heavier server pair `io10-worker40` recovers part of that gap but still tops out at only `169312.10 rps` (`conc=160, proc=4, wrk=2`), still `5.18%` below the winner and with `1056.62 us` p95.

So the broad conclusion from this chart is now cleaner: the anomalous `140k` ceiling is gone, but simply stacking more server threads is still not the answer. On this host the useful crest lives in the `io=5-6` region, and over-threading still costs about five to six percent of throughput while burning much more latency budget.

## 4. Peak Neighborhood

The next chart zooms into the densest near-peak family. In this run the exact winner belongs to the adjacent `io6-worker12-proc2-wrk1` family, but the reference curve is still the richer `io5-worker10-proc2-wrk1` family because it has the cleanest dense neighborhood around the crest.

![Peak Neighborhood](./benchmark-report-peak-neighborhood.png)

This curve shows a real near-peak interval, not a single spike:

- `conc=56` reaches `176229.73 rps` at `352.89 us` p95
- `conc=60` reaches `176688.50 rps` at `372.47 us` p95
- `conc=74` reaches `177242.17 rps` at `464.81 us` p95
- `conc=86` reaches `177750.67 rps` at `531.75 us` p95
- `conc=90` reaches `178219.00 rps` at `559.78 us` p95
- `conc=104` reaches `177785.67 rps` at `645.81 us` p95
- `conc=108` reaches `178264.67 rps` at `670.03 us` p95

Those points are all within `0.17%-0.74%` of the top cell. That is exactly the kind of spread that should be treated as one near-peak band under a single-host benchmark method.

Latency explains why this report should still describe a band, not a single number:

- `conc≈56-60` keeps p95 in the `353-372 us` range
- `conc≈74-90` stays near peak in throughput, but p95 rises into roughly `465-560 us`
- `conc≈104-108` is still within `0.17%-0.44%` of the top, but p95 is already `646-670 us`

So the practical crest is:

- lower-latency high-throughput crest: `conc≈56-60`
- balanced throughput crest: `conc≈74-90`
- extreme single-host throughput crest: `conc≈104-108`

## 5. Server Thread Sensitivity

The next chart fixes the client shape at `conc=80, proc=2, wrk=1` and varies server thread counts. This is the cleanest way to see whether the winner is a one-off.

![Thread Sensitivity](./benchmark-report-thread-sensitivity.png)

The answer is no. The winner is real, but the plateau around it is broad and non-monotonic:

- `io6-worker12-conc80-proc2-wrk1`: `178563.83 rps`, `501.22 us` p95
- `io6-worker7-conc80-proc2-wrk1`: `178238.09 rps`, `501.07 us` p95, only `0.18%` lower
- `io6-worker9-conc80-proc2-wrk1`: `177956.67 rps`, `503.72 us` p95, only `0.34%` lower
- `io5-worker15-conc80-proc2-wrk1`: `177712.83 rps`, `494.17 us` p95, only `0.48%` lower
- `io5-worker9-conc80-proc2-wrk1`: `177561.13 rps`, `492.39 us` p95, only `0.56%` lower

There is also an important non-monotonic detail: inside the `io=5` family, `worker=5` starts very strong at `176444.67 rps`, `worker=7` dips to `171625.33 rps`, and then `worker=14-15` climbs back to `177452.09-177712.83 rps`. That is the signature of a same-host thread scheduling tradeoff, not a simple "more workers is better" slope.

That gives a robust server-side conclusion:

- `io=5-6` is the clear center in this run
- the useful worker region at that center is broad, roughly `7-15`
- moving `io` down to `4` costs a few percent and removes some headroom
- moving `io` up to `10` still works, but costs about `6%-8%` of throughput and much more latency

For example:

- `io4-worker11-conc80-proc2-wrk1`: `171270.67 rps`, `510.53 us` p95, `4.08%` below the winner
- `io10-worker20-conc80-proc2-wrk1`: `164469.43 rps`, `606.26 us` p95, `7.89%` below the winner

## 6. Load Generator Sensitivity

Because this is a single-host run, client-side shape matters. The next chart fixes the server at the richest comparable pair, `io=5, worker=10`, and compares several `proc/wrk` families across concurrency.

![Load Generator Sensitivity](./benchmark-report-loadgen-sensitivity.png)

This chart is the strongest evidence that the benchmark method itself materially changes the measured ceiling:

- `proc=1,wrk=2` reaches `178442.71 rps` at `conc=60`, only `0.07%` below the top, with `371.11 us` p95
- `proc=2,wrk=1` reaches `178264.67 rps` at `conc=108`, only `0.17%` below the top, but with much higher `670.03 us` p95
- `proc=4,wrk=1` peaks at `175400.46 rps` at `conc=72`, already `1.77%` below the top
- `proc=2,wrk=2` peaks at `175917.74 rps` at `conc=60`, `1.48%` below the top
- the naive `proc=1,wrk=1` family never becomes competitive here; its best point is only `173270.25 rps` at `conc=88`, which is `2.96%` below the winner

This is exactly what a same-host benchmark should look like:

- server threads, client threads, and loopback sockets all compete on the same CPU pool
- a different wrk fan-out still moves the apparent ceiling by several percent
- small deltas inside the best families are useful for locating a band, but the ceiling is a property of the whole arrangement, not only the server

## 7. What This Means For Single-Host Benchmarking

This run is good enough to locate the near-peak region, and it cleanly replaces the earlier kernel-update outlier report. It should still not be used to overstate precision.

The disciplined way to read it is:

- The single-host benchmark is valid for relative tuning.
  It clearly separates the baseline, the near-peak server thread region, and the over-threaded region.
- The single-host benchmark is not a pure server ceiling.
  The best points still depend strongly on the wrk process/thread layout, which means the measurement includes wrk-side CPU and loopback overhead.
- The right output is a band, not a point.
  On this run the credible near-peak band is centered on `io=5-6`, `worker=7-15`, mainly with `concurrency=60-108`, and shaped by two dominant loadgen families: `proc=2,wrk=1` and `proc=1,wrk=2`.

If the next step is to estimate server-only headroom, the correct follow-up is still a dual-host run with the server fixed near this band and only the client side swept from a second machine.

## 8. Recommended Configurations

For this machine and this benchmark path:

- Best single-host point from this run:
  `io=6, worker=12, conc=80, proc=2, wrk=1`
- Best balanced near-peak point:
  `io=5, worker=10, conc=60, proc=1, wrk=2`
  It is only `0.07%` below the top, but p95 drops from `501.22 us` to `371.11 us`.
- Best lower-latency high-throughput point:
  `io=5, worker=10, conc=20, proc=1, wrk=2`
  It reaches `175478.23 rps` with only `127.50 us` p95, while staying just `1.73%` below the exact top.
- Best simpler high-throughput point if you want to keep the same `proc=2,wrk=1` client shape as the winner family:
  `io=5, worker=10, conc=74, proc=2, wrk=1`
  It reaches `177242.17 rps`, only `0.74%` below the top, with materially lower p95 than the most aggressive `90-108` points.

## 9. Artifacts

- Report: [benchmark-report.md](benchmark-report.md)
- Data: [benchmark-report.json](benchmark-report.json)
- Concise package: [package/summary.md](./package/summary.md)
