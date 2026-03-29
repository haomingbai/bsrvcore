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
| http_get_static | io6-worker12-conc80-proc2-wrk1 | stable | 178563.83 | 501.22 | 529.00 | 6 | 12 | 80 | 2 | 1 |

## Top Cells

| rank | scenario | pressure | stability | mean_rps | p95_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | http_get_static | io6-worker12-conc80-proc2-wrk1 | stable | 178563.83 | 501.22 | 6 | 12 | 80 | 2 | 1 |
| 2 | http_get_static | io5-worker10-conc60-proc1-wrk2 | stable | 178442.71 | 371.11 | 5 | 10 | 60 | 1 | 2 |
| 3 | http_get_static | io5-worker10-conc108-proc2-wrk1 | stable | 178264.67 | 670.03 | 5 | 10 | 108 | 2 | 1 |
| 4 | http_get_static | io6-worker7-conc80-proc2-wrk1 | stable | 178238.09 | 501.07 | 6 | 7 | 80 | 2 | 1 |
| 5 | http_get_static | io5-worker10-conc90-proc2-wrk1 | stable | 178219.00 | 559.78 | 5 | 10 | 90 | 2 | 1 |
| 6 | http_get_static | io6-worker9-conc80-proc2-wrk1 | stable | 177956.67 | 503.72 | 6 | 9 | 80 | 2 | 1 |
| 7 | http_get_static | io5-worker10-conc104-proc2-wrk1 | stable | 177785.67 | 645.81 | 5 | 10 | 104 | 2 | 1 |
| 8 | http_get_static | io5-worker10-conc86-proc2-wrk1 | stable | 177750.67 | 531.75 | 5 | 10 | 86 | 2 | 1 |
| 9 | http_get_static | io5-worker15-conc80-proc2-wrk1 | stable | 177712.83 | 494.17 | 5 | 15 | 80 | 2 | 1 |
| 10 | http_get_static | io6-worker8-conc80-proc2-wrk1 | stable | 177578.83 | 510.33 | 6 | 8 | 80 | 2 | 1 |

## Sweep Space

| dimension | values |
| --- | --- |
| scenario | http_get_static |
| server_io_threads | 1, 3, 4, 5, 6, 7, 10 |
| server_worker_threads | 1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 20, 40 |
| client_concurrency | 1, 2, 4, 8, 10, 20, 40, 48, 52, 56, 60, 64, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 96, 100, 104, 108, 112, 160, 320 |
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

