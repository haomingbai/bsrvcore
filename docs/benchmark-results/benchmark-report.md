# Bsrvcore HTTP Benchmark Report (2026-04-17 Refresh)

## 1. Current Outcome

This refresh reran the mainline benchmark, then measured GET and POST body behavior around the current hot path.

The mainline winner this time is:

- `io20-worker1-conc170-proc4-wrk2`
- `824608.14 rps`
- `p95 3270.41 us`, `p99 4555.38 us`
- stability: `stable`

The previous report winner was:

- `io18-worker1-conc160-proc4-wrk2`
- `861148.07 rps`

Comparison:

- throughput delta: `-4.24%`
- combined historical/current plots: not used

The mainline result did not improve, so this refresh kept the plots for the current run only.

![Capacity Overview](./benchmark-report-capacity-overview.png)

![Per-Connection Throughput](./benchmark-report-per-connection-throughput.png)

## 2. Mainline Scan

The mainline scan used coarse-to-fine refinement and finished with a stable plateau around `io20-worker1-conc170-proc4-wrk2`.

Key points from the scan path were:

- coarse seed: `io10-worker20-conc160-proc2-wrk1`, `575572.24 rps`
- first refinement: `io20-worker4-conc160-proc4-wrk2`, `831786.47 rps`
- second refinement: `io18-worker1-conc160-proc4-wrk2`, `891785.90 rps`
- third refinement: `io18-worker1-conc136-proc4-wrk2`, `864880.80 rps`
- final confirmation winner: `io20-worker1-conc170-proc4-wrk2`, `824608.14 rps`

The neighborhood remained tight rather than spiky, and the final confirmation favored the `conc=170` point by a narrow margin.

![Thread Sensitivity](./benchmark-report-thread-sensitivity.png)

![Load Generator Sensitivity](./benchmark-report-loadgen-sensitivity.png)

![Peak Neighborhood](./benchmark-report-peak-neighborhood.png)

## 3. GET Response-Body Sweep

The GET body run used a focused matrix and reached a stable winner at:

- `io20-worker1-conc256-proc4-wrk2-req0-resp0`
- `742756.95 rps`
- `p95 4471.83 us`
- stability: `stable`

At fixed `conc=256`, throughput dropped smoothly as response size increased:

| response body | mean_rps | per_conn_rps | p95_us | stability |
| --- | ---: | ---: | ---: | --- |
| `0` | `742756.95` | `2901.39` | `4471.83` | stable |
| `8 KiB` | `613206.74` | `2395.34` | `4474.27` | stable |
| `16 KiB` | `536977.42` | `2097.57` | `3990.89` | stable |
| `32 KiB` | `414723.39` | `1620.01` | `4079.80` | stable |
| `64 KiB` | `265739.45` | `1038.04` | `4087.32` | stable |
| `128 KiB` | `135406.13` | `528.93` | `5183.02` | stable |
| `256 KiB` | `55933.33` | `218.49` | `7214.26` | stable |

The GET path stayed stable across the full range. The curve is bandwidth-shaped: throughput falls with body size, while tail latency rises gradually rather than hitting a sudden cliff.

![GET Response Curves](./benchmark-report-body-get-response-curves.png)

![GET Response Slices](./benchmark-report-body-get-response-slices.png)

## 4. POST Request-Body Probes

POST request bodies were measured with representative probes on the same `io20-worker1-conc64-proc4-wrk2` region and one higher-concurrency boundary point.

Stable points at `conc=64` were:

| point | mean_rps | p95_us | stability |
| --- | ---: | ---: | --- |
| `req=0` | `102523.55` | `1554.54` | stable |
| `req=8 KiB` | `104578.23` | `1478.19` | stable |
| `req=16 KiB` | `104304.35` | `1526.29` | stable |
| `req=24 KiB` | `106148.71` | `1485.92` | stable |

The boundary point at higher concurrency behaved very differently:

| point | mean_rps | p95_us | stability |
| --- | ---: | ---: | --- |
| `conc=128, req=32 KiB` | `88.00` | `7056.70` | unstable |

That split shows a clear stable band at moderate concurrency, plus a sharp boundary region at larger request sizes.

![POST Long Frontier](./benchmark-report-body-post-long-frontier.png)

![POST Long Heatmap](./benchmark-report-body-post-long-heatmap.png)

![POST Short Heatmap](./benchmark-report-body-post-short-heatmap.png)

## 5. Environment And Scope

- CPU: `13th Gen Intel(R) Core(TM) i9-13900H`
- logical CPUs: `20`
- OS: `Fedora Linux 43 (Workstation Edition)`
- kernel: `6.19.11-200.fc43.x86_64`
- build: `Release`
- topology: single-host loopback

The report excludes hostname and IP details.

## 6. Artifact Index

Published report artifacts:

- `docs/benchmark-results/benchmark-report.md`
- `docs/benchmark-results/benchmark-report.json`
- `docs/benchmark-results/mainline-20260417-standard/`
- `docs/benchmark-results/body-get-20260417/`

Raw benchmark data remains under `.artifacts/benchmark-results/`.
