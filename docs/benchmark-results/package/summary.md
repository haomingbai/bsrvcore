# Benchmark Package Summary

This package is intentionally concise. It records the benchmark facts, the winner, and the chart index.

## Run

- mode: `local`
- topology: `single-host`
- scenario: `http_get_static`
- sweep_depth: `quick`
- server_url: `local-started-per-cell`
- benchmark_command: `bash scripts/benchmark.sh run --scenario http_get_static --sweep-depth quick --output-dir .artifacts/benchmark-results/local-official-dispatch-20260401-125139`
- cell_count: `108`

## Winner

| scenario | pressure | stability | mean_rps | p95_us | p99_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| http_get_static | io10-worker20-conc56-proc2-wrk2 | stable | 423307.75 | 353.86 | 437.92 | 10 | 20 | 56 | 2 | 2 |

## Top Cells

| rank | scenario | pressure | stability | mean_rps | p95_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | http_get_static | io10-worker20-conc56-proc2-wrk2 | stable | 423307.75 | 353.86 | 10 | 20 | 56 | 2 | 2 |
| 2 | http_get_static | io10-worker20-conc48-proc4-wrk2 | stable | 414470.48 | 356.18 | 10 | 20 | 48 | 4 | 2 |
| 3 | http_get_static | io10-worker20-conc40-proc4-wrk2 | stable | 413740.71 | 387.37 | 10 | 20 | 40 | 4 | 2 |
| 4 | http_get_static | io10-worker20-conc56-proc3-wrk1 | stable | 406145.75 | 288.06 | 10 | 20 | 56 | 3 | 1 |
| 5 | http_get_static | io10-worker20-conc48-proc3-wrk1 | stable | 405371.25 | 240.72 | 10 | 20 | 48 | 3 | 1 |
| 6 | http_get_static | io10-worker20-conc56-proc4-wrk1 | stable | 397552.38 | 311.00 | 10 | 20 | 56 | 4 | 1 |
| 7 | http_get_static | io10-worker20-conc48-proc2-wrk2 | stable | 392192.50 | 340.71 | 10 | 20 | 48 | 2 | 2 |
| 8 | http_get_static | io10-worker20-conc48-proc4-wrk1 | stable | 388097.14 | 388.65 | 10 | 20 | 48 | 4 | 1 |
| 9 | http_get_static | io10-worker20-conc32-proc4-wrk2 | stable | 386034.05 | 247.16 | 10 | 20 | 32 | 4 | 2 |
| 10 | http_get_static | io10-worker20-conc40-proc3-wrk1 | stable | 383989.00 | 209.31 | 10 | 20 | 40 | 3 | 1 |

## Sweep Space

| dimension | values |
| --- | --- |
| scenario | http_get_static |
| server_io_threads | 1, 5, 8, 9, 10, 11, 12 |
| server_worker_threads | 1, 10, 16, 17, 18, 19, 20, 21, 22, 23, 24 |
| client_concurrency | 1, 2, 4, 8, 10, 16, 20, 24, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 56, 60, 64, 80, 160 |
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

