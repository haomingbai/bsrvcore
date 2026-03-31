# Benchmark Package Summary

This package is intentionally concise. It records the benchmark facts, the winner, and the chart index.

## Run

- mode: `client`
- topology: `dual-host-ssh`
- scenario: `http_get_static`
- sweep_depth: `standard`
- server_url: `http://10.68.170.210:18080`
- benchmark_command: `bash scripts/benchmark.sh ssh-run --scenario http_get_static --ssh-target server --ssh-remote-root /home/haomingbai/bsrvcore --server-host 10.68.170.210 --build-dir build --sweep-depth standard --output-dir /home/haomingbai/my_projects/bsrvcore/.artifacts/benchmark-results/20260331-dual-host-standard-refined`
- cell_count: `148`

## Winner

| scenario | pressure | stability | mean_rps | p95_us | p99_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| http_get_static | io24-worker96-conc412-proc4-wrk2 | stable | 44247.33 | 24942.84 | 35918.12 | 24 | 96 | 412 | 4 | 2 |

## Top Cells

| rank | scenario | pressure | stability | mean_rps | p95_us | server_io_threads | server_worker_threads | client_concurrency | client_processes | wrk_threads_per_process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | http_get_static | io24-worker96-conc412-proc4-wrk2 | stable | 44247.33 | 24942.84 | 24 | 96 | 412 | 4 | 2 |
| 2 | http_get_static | io24-worker96-conc384-proc3-wrk2 | stable | 43687.01 | 20529.11 | 24 | 96 | 384 | 3 | 2 |
| 3 | http_get_static | io25-worker97-conc384-proc4-wrk2 | stable | 43250.83 | 22609.08 | 25 | 97 | 384 | 4 | 2 |
| 4 | http_get_static | io24-worker98-conc384-proc4-wrk2 | stable | 43236.09 | 24475.01 | 24 | 98 | 384 | 4 | 2 |
| 5 | http_get_static | io25-worker93-conc384-proc4-wrk2 | stable | 43226.04 | 22267.75 | 25 | 93 | 384 | 4 | 2 |
| 6 | http_get_static | io24-worker96-conc416-proc4-wrk2 | stable | 43210.63 | 26851.49 | 24 | 96 | 416 | 4 | 2 |
| 7 | http_get_static | io24-worker96-conc408-proc4-wrk2 | stable | 43170.03 | 24717.60 | 24 | 96 | 408 | 4 | 2 |
| 8 | http_get_static | io24-worker96-conc400-proc4-wrk2 | stable | 43038.60 | 27724.19 | 24 | 96 | 400 | 4 | 2 |
| 9 | http_get_static | io24-worker96-conc384-proc1-wrk2 | stable | 43012.03 | 19470.00 | 24 | 96 | 384 | 1 | 2 |
| 10 | http_get_static | io24-worker96-conc384-proc4-wrk2 | stable | 42959.60 | 22321.59 | 24 | 96 | 384 | 4 | 2 |

## Sweep Space

| dimension | values |
| --- | --- |
| scenario | http_get_static |
| server_io_threads | 1, 12, 22, 23, 24, 25, 26 |
| server_worker_threads | 1, 24, 48, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101 |
| client_concurrency | 1, 2, 4, 8, 24, 48, 96, 192, 288, 352, 356, 360, 364, 368, 372, 374, 376, 378, 380, 382, 384, 386, 388, 390, 392, 394, 396, 400, 404, 408, 412, 416, 480, 768 |
| client_processes | 1, 2, 3, 4 |
| wrk_threads_per_process | 1, 2 |

## Environment

- client_hostname: `haomingbai-PC`
- client_cpus: `20`
- client_uname: `Linux haomingbai-PC 6.19.8-200.fc43.x86_64 #1 SMP PREEMPT_DYNAMIC Fri Mar 13 22:06:06 UTC 2026 x86_64 GNU/Linux`
- server_hostname: `ubuntu-SYS-7048GR-TR`
- server_cpus: `48`
- server_uname: `Linux ubuntu-SYS-7048GR-TR 6.8.0-106-generic #106-Ubuntu SMP PREEMPT_DYNAMIC Fri Mar  6 07:58:08 UTC 2026 x86_64 x86_64 x86_64 GNU/Linux`

## Artifacts

- consolidated_json: [../benchmark-report.json](../benchmark-report.json)
- technical_report: [../benchmark-report.md](../benchmark-report.md)
- charts: read the embedded images from the technical report in the parent directory
- targeted_extension: [../../../.artifacts/benchmark-results/20260401-dual-host-extended-wrk-io/summary.md](../../../.artifacts/benchmark-results/20260401-dual-host-extended-wrk-io/summary.md)
