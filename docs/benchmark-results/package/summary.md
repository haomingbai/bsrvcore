# Benchmark Package Summary

This package is intentionally concise. It records the benchmark facts, the winner, and the chart index.

## Run

- mode: `local`
- topology: `single-host`
- scenario: `http_get_static`
- sweep_depth: `standard`
- server_url: `local-started-per-cell`
- benchmark_command: `bash scripts/benchmark.sh run --scenario http_get_static --sweep-depth standard`
- cell_count: `146`

## Winner

| scenario | pressure | stability | mean_rps | p95_us | p99_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| http_get_static | io5-worker10-conc20-proc2-wrk1 | stable | 140095.48 | 167.26 | 181.28 | 5 | 10 | 20 | 2 | 1 |

## Top Cells

| rank | scenario | pressure | stability | mean_rps | p95_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | http_get_static | io5-worker10-conc20-proc2-wrk1 | stable | 140095.48 | 167.26 | 5 | 10 | 20 | 2 | 1 |
| 2 | http_get_static | io5-worker10-conc28-proc1-wrk2 | stable | 139717.50 | 234.83 | 5 | 10 | 28 | 1 | 2 |
| 3 | http_get_static | io5-worker10-conc36-proc2-wrk1 | stable | 139675.33 | 309.86 | 5 | 10 | 36 | 2 | 1 |
| 4 | http_get_static | io5-worker15-conc20-proc2-wrk1 | stable | 139324.00 | 168.22 | 5 | 15 | 20 | 2 | 1 |
| 5 | http_get_static | io5-worker12-conc20-proc2-wrk1 | stable | 139137.47 | 167.47 | 5 | 12 | 20 | 2 | 1 |
| 6 | http_get_static | io5-worker13-conc20-proc2-wrk1 | stable | 139037.90 | 169.73 | 5 | 13 | 20 | 2 | 1 |
| 7 | http_get_static | io5-worker10-conc30-proc2-wrk1 | stable | 139037.50 | 250.00 | 5 | 10 | 30 | 2 | 1 |
| 8 | http_get_static | io5-worker10-conc24-proc2-wrk1 | stable | 138965.83 | 203.64 | 5 | 10 | 24 | 2 | 1 |
| 9 | http_get_static | io5-worker8-conc20-proc2-wrk1 | stable | 138872.26 | 170.22 | 5 | 8 | 20 | 2 | 1 |
| 10 | http_get_static | io5-worker10-conc14-proc2-wrk1 | stable | 138849.35 | 122.53 | 5 | 10 | 14 | 2 | 1 |

## Sweep Space

| dimension | values |
| --- | --- |
| scenario | http_get_static |
| server_io_threads | 1, 3, 4, 5, 6, 7, 10 |
| server_worker_threads | 1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 20, 40 |
| client_concurrency | 1, 2, 4, 5, 8, 10, 12, 14, 15, 16, 18, 20, 22, 24, 25, 26, 28, 30, 32, 36, 40, 44, 48, 52, 80, 160, 320 |
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

