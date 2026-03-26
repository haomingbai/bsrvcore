# Benchmark Package Summary

This package is intentionally concise. It records the benchmark facts, the winner, and the chart index.

## Run

- mode: `local`
- topology: `single-host`
- scenario: `http_get_static`
- sweep_depth: `quick`
- server_url: `local-started-per-cell`
- benchmark_command: `bash scripts/benchmark.sh run --scenario http_get_static --sweep-depth quick`
- cell_count: `106`

## Winner

| scenario | pressure | stability | mean_rps | p95_us | p99_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| http_get_static | io5-worker11-conc40-proc2-wrk1 | stable | 184256.43 | 244.88 | 259.99 | 5 | 11 | 40 | 2 | 1 |

## Top Cells

| rank | scenario | pressure | stability | mean_rps | p95_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | http_get_static | io5-worker11-conc40-proc2-wrk1 | stable | 184256.43 | 244.88 | 5 | 11 | 40 | 2 | 1 |
| 2 | http_get_static | io5-worker10-conc56-proc1-wrk2 | stable | 183682.24 | 341.44 | 5 | 10 | 56 | 1 | 2 |
| 3 | http_get_static | io5-worker10-conc40-proc1-wrk2 | stable | 183445.24 | 244.83 | 5 | 10 | 40 | 1 | 2 |
| 4 | http_get_static | io5-worker10-conc60-proc2-wrk1 | stable | 182486.30 | 373.73 | 5 | 10 | 60 | 2 | 1 |
| 5 | http_get_static | io6-worker11-conc40-proc2-wrk1 | stable | 182484.93 | 250.17 | 6 | 11 | 40 | 2 | 1 |
| 6 | http_get_static | io5-worker10-conc38-proc2-wrk1 | stable | 182334.57 | 258.12 | 5 | 10 | 38 | 2 | 1 |
| 7 | http_get_static | io5-worker10-conc40-proc2-wrk1 | stable | 182202.62 | 275.82 | 5 | 10 | 40 | 2 | 1 |
| 8 | http_get_static | io5-worker10-conc56-proc1-wrk1 | stable | 182196.75 | 344.11 | 5 | 10 | 56 | 1 | 1 |
| 9 | http_get_static | io5-worker10-conc56-proc4-wrk2 | stable | 181967.86 | 345.00 | 5 | 10 | 56 | 4 | 2 |
| 10 | http_get_static | io5-worker10-conc56-proc4-wrk1 | stable | 181861.43 | 346.10 | 5 | 10 | 56 | 4 | 1 |

## Sweep Space

| dimension | values |
| --- | --- |
| scenario | http_get_static |
| server_io_threads | 1, 3, 4, 5, 6, 7, 10 |
| server_worker_threads | 1, 6, 7, 8, 9, 10, 11, 12, 13, 14, 20 |
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

