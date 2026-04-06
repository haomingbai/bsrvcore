# Benchmark Package Summary

This package is intentionally concise. It records the benchmark facts, the winner, and the chart index.

## Run

- mode: `local`
- topology: `single-host`
- scenario: `http_get_static`
- sweep_depth: `quick`
- server_url: `local-started-per-cell`
- benchmark_command: `bash scripts/benchmark.sh run --scenario http_get_static --sweep-depth quick --output-dir .artifacts/benchmark-results/local-main-sweepfix-20260406-054218Z`
- cell_count: `306`

## Winner

| scenario | pressure | stability | mean_rps | p95_us | p99_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| http_get_static | io15-worker1-conc88-proc3-wrk2 | stable | 477092.21 | 2492.46 | 4119.96 | 15 | 1 | 88 | 3 | 2 |

## Top Cells

| rank | scenario | pressure | stability | mean_rps | p95_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | http_get_static | io15-worker1-conc88-proc3-wrk2 | stable | 477092.21 | 2492.46 | 15 | 1 | 88 | 3 | 2 |
| 2 | http_get_static | io15-worker1-conc80-proc4-wrk2 | stable | 475384.56 | 2647.37 | 15 | 1 | 80 | 4 | 2 |
| 3 | http_get_static | io15-worker1-conc80-proc3-wrk2 | stable | 468269.05 | 2110.36 | 15 | 1 | 80 | 3 | 2 |
| 4 | http_get_static | io20-worker1-conc80-proc3-wrk2 | stable | 467945.51 | 2243.86 | 20 | 1 | 80 | 3 | 2 |
| 5 | http_get_static | io20-worker1-conc88-proc3-wrk2 | stable | 467697.62 | 2497.80 | 20 | 1 | 88 | 3 | 2 |
| 6 | http_get_static | io15-worker1-conc88-proc4-wrk2 | stable | 466766.19 | 2319.22 | 15 | 1 | 88 | 4 | 2 |
| 7 | http_get_static | io20-worker1-conc88-proc4-wrk2 | stable | 466449.52 | 2771.41 | 20 | 1 | 88 | 4 | 2 |
| 8 | http_get_static | io24-worker1-conc88-proc3-wrk2 | stable | 466206.91 | 2636.89 | 24 | 1 | 88 | 3 | 2 |
| 9 | http_get_static | io15-worker1-conc72-proc3-wrk2 | stable | 464276.91 | 2129.34 | 15 | 1 | 72 | 3 | 2 |
| 10 | http_get_static | io12-worker1-conc88-proc4-wrk2 | stable | 463913.41 | 1127.97 | 12 | 1 | 88 | 4 | 2 |

## Sweep Space

| dimension | values |
| --- | --- |
| scenario | http_get_static |
| server_io_threads | 1, 5, 8, 9, 10, 11, 12, 15, 20, 24 |
| server_worker_threads | 1, 2, 3, 4, 5, 8, 10, 20 |
| client_concurrency | 1, 2, 4, 8, 10, 15, 20, 24, 30, 40, 48, 56, 60, 64, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 96, 100, 104, 120, 160, 192 |
| client_processes | 1, 2, 3, 4 |
| wrk_threads_per_process | 1, 2 |

## Environment

- client_hostname: `haomingbai-PC`
- client_cpus: `20`
- client_uname: `Linux haomingbai-PC 6.19.8-200.fc43.x86_64 #1 SMP PREEMPT_DYNAMIC Fri Mar 13 22:06:06 UTC 2026 x86_64 GNU/Linux`

## Artifacts

- consolidated_json: [../benchmark-report.json](../benchmark-report.json)
- technical_report: [../benchmark-report.md](../benchmark-report.md)
- charts: read the embedded images from the technical report in the parent directory

