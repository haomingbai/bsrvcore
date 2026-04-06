# Benchmark Package Summary

This package is intentionally concise. It records the benchmark facts, the winner, and the chart index.

## Run

- mode: `local`
- topology: `single-host`
- scenario: `http_get_static`
- sweep_depth: `standard`
- server_url: `local-started-per-cell`
- benchmark_command: `bash scripts/benchmark.sh run --scenario http_get_static --sweep-depth standard --output-dir .artifacts/benchmark-results/local-main-resweep-20260406-081136Z`
- cell_count: `399`

## Winner

| scenario | pressure | stability | mean_rps | p95_us | p99_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| http_get_static | io20-worker4-conc160-proc3-wrk2 | stable | 730645.97 | 2638.58 | 4008.67 | 20 | 4 | 160 | 3 | 2 |

## Top Cells

| rank | scenario | pressure | stability | mean_rps | p95_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | http_get_static | io20-worker4-conc160-proc3-wrk2 | stable | 730645.97 | 2638.58 | 20 | 4 | 160 | 3 | 2 |
| 2 | http_get_static | io20-worker1-conc200-proc4-wrk2 | stable | 730166.40 | 3475.55 | 20 | 1 | 200 | 4 | 2 |
| 3 | http_get_static | io20-worker2-conc160-proc4-wrk2 | stable | 727939.56 | 2971.49 | 20 | 2 | 160 | 4 | 2 |
| 4 | http_get_static | io20-worker1-conc176-proc3-wrk2 | stable | 727400.81 | 2644.90 | 20 | 1 | 176 | 3 | 2 |
| 5 | http_get_static | io19-worker1-conc160-proc4-wrk2 | stable | 726856.45 | 3245.66 | 19 | 1 | 160 | 4 | 2 |
| 6 | http_get_static | io19-worker3-conc160-proc4-wrk2 | stable | 726686.89 | 2904.74 | 19 | 3 | 160 | 4 | 2 |
| 7 | http_get_static | io20-worker1-conc176-proc4-wrk2 | stable | 726012.58 | 3355.40 | 20 | 1 | 176 | 4 | 2 |
| 8 | http_get_static | io20-worker4-conc168-proc3-wrk2 | stable | 724874.99 | 2799.10 | 20 | 4 | 168 | 3 | 2 |
| 9 | http_get_static | io20-worker1-conc168-proc3-wrk2 | stable | 724862.10 | 2343.31 | 20 | 1 | 168 | 3 | 2 |
| 10 | http_get_static | io20-worker1-conc160-proc3-wrk2 | stable | 722904.84 | 2168.20 | 20 | 1 | 160 | 3 | 2 |

## Sweep Space

| dimension | values |
| --- | --- |
| scenario | http_get_static |
| server_io_threads | 1, 5, 10, 15, 18, 19, 20, 21, 22, 24, 25, 30, 40, 48 |
| server_worker_threads | 1, 2, 3, 4, 5, 6, 8, 10, 20, 40 |
| client_concurrency | 1, 2, 4, 5, 8, 10, 15, 20, 24, 30, 40, 48, 60, 80, 96, 120, 128, 132, 136, 140, 144, 148, 150, 152, 154, 156, 158, 160, 162, 164, 166, 168, 170, 172, 176, 180, 184, 188, 192, 200, 320 |
| client_processes | 1, 2, 3, 4 |
| wrk_threads_per_process | 1, 2 |

## Environment

- client_cpus: `20`
- client_uname: `Linux haomingbai-PC 6.19.8-200.fc43.x86_64 #1 SMP PREEMPT_DYNAMIC Fri Mar 13 22:06:06 UTC 2026 x86_64 GNU/Linux`

## Artifacts

- consolidated_json: [../benchmark-report.json](../benchmark-report.json)
- technical_report: [../benchmark-report.md](../benchmark-report.md)
- charts: read the embedded images from the technical report in the parent directory

