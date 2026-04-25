# Benchmark Package Summary

This package is the published summary for the 2026-04-25 benchmark refresh.

## Mainline Winner

- pressure: `io15-worker1-conc144-proc4-wrk2`
- scenario: `http_get_static`
- `835092.66 rps`
- `5799.25 rps / connection`
- `p95 3111.71 us`, `p99 4421.86 us`
- stability: `stable`

## Comparison To Previous Report

- previous winner: `io20-worker1-conc170-proc4-wrk2`, `824608.14 rps`
- throughput delta: `+1.27%` (`+10484.52 rps`)
- p95 delta: `-158.70 us`
- conclusion: current run improved in both throughput and tail latency

## Body-Size Highlights

- GET response-size probes were stable across `0 .. 256 KiB` at fixed `conc=256`
- GET throughput remained monotonic (`851732.65 -> 58335.78 rps` from `0B` to `256 KiB`)
- POST showed a stable local band at `conc=64` for `req<=16 KiB`
- POST `conc=64, req=24 KiB` became unstable in this refresh
- POST `conc=128, req=32 KiB` remained unstable and is treated as a boundary point

## Published Files

- report: [../benchmark-report.md](../benchmark-report.md)
- report manifest: [../benchmark-report.json](../benchmark-report.json)
- sanitized environment: [./client-env.json](./client-env.json)
- charts:
  - `benchmark-report-capacity-overview.png`
  - `benchmark-report-per-connection-throughput.png`
  - `benchmark-report-loadgen-sensitivity.png`
  - `benchmark-report-peak-neighborhood.png`
  - `benchmark-report-mainline-comparison.png`
  - `benchmark-report-body-get-conc256.png`
  - `benchmark-report-body-post-probes.png`
