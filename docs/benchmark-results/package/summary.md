# Benchmark Package Summary

This package is the published summary for the 2026-04-12 benchmark refresh.

## Mainline Winner

- pressure: `io18-worker1-conc160-proc4-wrk2`
- scenario: `http_get_static`
- `861148.07 rps`
- `5382.18 rps / connection`
- `p95 3171.76 us`, `p99 4419.79 us`
- stability: `stable`

## Mainline Scan Path

- old winner peek: `io20-worker4-conc160-proc3-wrk2`, `854348.07 rps`
- coarse-only winner: `io10-worker20-conc160-proc2-wrk1`, `575572.24 rps`
- final winner gain over coarse-only: `+49.62%`
- final winner gain over old winner peek: `+0.80%`

## Body-Size Highlights

- GET response-size sweep stayed smooth and stable up to `256 KiB`
- at `conc=160`, GET throughput fell from `821970.97 rps` at `0B` to `68013.15 rps` at `256 KiB`
- POST request-size sweep did not produce a monotonic curve
- reproducible POST platforms were confirmed at:
  - `conc=64, req=16 KiB`, `207204.91 rps`
  - `conc=64, req=24 KiB`, `200045.34 rps`
  - `conc=80, req=32 KiB`, `212660.22 rps`
- a short-window high at `conc=48, req=8 KiB` did not survive long-window confirmation

## Published Files

- report: [../benchmark-report.md](../benchmark-report.md)
- report manifest: [../benchmark-report.json](../benchmark-report.json)
- sanitized environment: [./client-env.json](./client-env.json)
- charts:
  - `benchmark-report-capacity-overview.png`
  - `benchmark-report-per-connection-throughput.png`
  - `benchmark-report-thread-sensitivity.png`
  - `benchmark-report-loadgen-sensitivity.png`
  - `benchmark-report-peak-neighborhood.png`
  - `benchmark-report-body-get-response-curves.png`
  - `benchmark-report-body-get-response-slices.png`
  - `benchmark-report-body-post-long-frontier.png`
  - `benchmark-report-body-post-long-heatmap.png`
  - `benchmark-report-body-post-short-heatmap.png`
