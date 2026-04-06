# Bsrvcore Main-Branch HTTP Benchmark Report (CPU Governor Retest)

## 1. Current Outcome

This run re-benchmarks `main` under single-host loopback for `http_get_static` after CPU governor tuning.

Complete package:

- command: `bash scripts/benchmark.sh run --scenario http_get_static --sweep-depth standard --output-dir .artifacts/benchmark-results/local-main-resweep-20260406-081136Z`
- coarse cells: `138`
- fine cells near peak: `261`
- total cells: `399`
- final winner: `io20-worker4-conc160-proc3-wrk2`
- `mean_rps = 730645.97`
- `p95 = 2638.58 us`, `p99 = 4008.67 us`
- stability: `stable`

![Capacity Overview](./benchmark-report-capacity-overview.png)

## 2. Coarse Then Fine (Peak-Nearby) Validation

This run follows the requested method strictly:

- coarse first: low-worker / high-io combinations plus loadgen shapes to locate the peak region
- fine second: only around coarse winner neighborhood

Coarse stage winner (`138` cells):

- `io20-worker1-conc160-proc4-wrk2`
- `mean_rps = 719473.17`, `p95 = 3014.79 us`, `p99 = 4288.88 us`

Fine stage then explored nearby points (including worker variations around `io=20`) and promoted:

- from coarse `worker=1` to final `worker=4`
- final winner gain over coarse winner: about `+1.55%`

This confirms the current best point is not a global random outlier; it is a local optimum found by coarse-to-fine refinement.

## 3. Thread And Loadgen Findings

Top stable cells are concentrated in:

- server io threads around `19-22`
- worker threads around `1-4`
- high loadgen shape (`proc=3/4`, `wrk=2`) with `concurrency` near `160-200`

Top stable cells (excerpt):

| rank | pressure | mean_rps | p95_us | p99_us |
| ---: | --- | ---: | ---: | ---: |
| 1 | `io20-worker4-conc160-proc3-wrk2` | 730645.97 | 2638.58 | 4008.67 |
| 2 | `io20-worker1-conc200-proc4-wrk2` | 730166.40 | 3475.55 | 4792.71 |
| 3 | `io20-worker2-conc160-proc4-wrk2` | 727939.56 | 2971.49 | 4198.27 |
| 4 | `io20-worker1-conc176-proc3-wrk2` | 727400.81 | 2644.90 | 4093.79 |
| 5 | `io19-worker1-conc160-proc4-wrk2` | 726856.45 | 3245.66 | 4550.14 |

Worker neighborhood check at fixed `io20-conc160-proc3-wrk2`:

| worker | mean_rps | p95_us | p99_us |
| ---: | ---: | ---: | ---: |
| 1 | 722904.84 | 2168.20 | 3592.20 |
| 2 | 717047.74 | 2687.89 | 4135.64 |
| 4 | 730645.97 | 2638.58 | 4008.67 |

Interpretation:

- `worker=4` wins throughput in this local neighborhood.
- `worker=1` is still competitive and has better p95/p99 at the same fixed shape.
- so, for this run, `worker=4` is throughput winner but not latency winner.

![Thread Sensitivity](./benchmark-report-thread-sensitivity.png)

## 4. Why This Run Jumps Above 500k

Observed from data only:

- high-RPS points now appear broadly across multiple IO bands (`15`, `19`, `20`, `24`, etc.) instead of a narrow single point
- loadgen shape sensitivity becomes much stronger: `proc=3/4 + wrk=2` is repeatedly required for >700k class results
- low-loadgen shapes (for example `proc=1`) remain much lower even at high server IO threads

Likely causes under loopback:

1. CPU governor tuning raised sustained available compute on both server and local load generator, removing part of prior loadgen-side throttling.
2. After that bottleneck moved, the sweep could expose a higher local optimum where kernel network path and scheduling dominate.
3. Peak throughput and tail latency now trade off more aggressively: the top-throughput cluster carries millisecond-level p95/p99.

Important note:

- this report treats the top point as the benchmark winner for throughput only;
- it is not automatically the best production operating point without a latency SLO.

## 5. Practical Winner vs. Latency Counterpoint

Throughput winner:

- `io20-worker4-conc160-proc3-wrk2`
- `730645.97 rps`, `p95 2638.58 us`, `p99 4008.67 us`

Low-tail counterpoint (same result set, `p95 < 500 us`):

- `io15-worker1-conc120-proc4-wrk1`
- `631292.26 rps`, `p95 292.62 us`, `p99 322.59 us`

That counterpoint is about `13.60%` lower throughput, but tail latency is much tighter.

![Peak Neighborhood](./benchmark-report-peak-neighborhood.png)

![Load Generator Sensitivity](./benchmark-report-loadgen-sensitivity.png)

## 6. Environment And Scope

- CPU: `13th Gen Intel(R) Core(TM) i9-13900H` (`20` logical CPUs)
- OS/kernel: `Fedora Linux 43`, `Linux 6.19.8-200.fc43.x86_64`
- build/profile: `main`, single-host loopback, scenario `http_get_static`

This package is method-compatible with previous reports, so the comparison is meaningful at methodology level.

## 7. Artifact Index

- report data: `benchmark-report.json`
- package summary: `package/summary.md`
- environment snapshot: `package/client-env.json`
- charts:
  - `benchmark-report-capacity-overview.png`
  - `benchmark-report-peak-neighborhood.png`
  - `benchmark-report-thread-sensitivity.png`
  - `benchmark-report-loadgen-sensitivity.png`