# 2026-03-24 Runtime Model Quick Benchmark (Release)

## Summary

- Build type: Release
- Benchmark profile: quick
- Scenario selector: all
- Repetitions: 2
- Pressure cells: light, saturated
- client_processes: 2
- wrk_threads_per_process: 1
- loadgen failures: 0 for all cells in this run

## Command

./build/benchmarks/bsrvcore_http_benchmark --scenario all --profile quick --output-json docs/benchmark-results/2026-03-24-runtime-model-quick.json

## Environment

- OS: Linux 6.19.6-200.fc43.x86_64 x86_64
- Compiler: GNU 15.2.1
- Logical CPUs: 20
- wrk path: /home/haomingbai/my_projects/bsrvcore/build/_deps/bsrvcore_benchmark_wrk/src/bsrvcore_benchmark_wrk/wrk

## Key Results (median)

- http_get_static/light: rps=22155.08, p95_us=56.28, stability=unstable
- http_get_static/saturated: rps=123971.50, p95_us=1695.56, stability=stable
- http_get_route_param/light: rps=9930.15, p95_us=62.44, stability=unstable
- http_get_route_param/saturated: rps=123696.33, p95_us=1699.72, stability=stable
- http_get_global_aspect/light: rps=11252.66, p95_us=66.56, stability=unstable
- http_get_global_aspect/saturated: rps=125722.33, p95_us=1592.77, stability=stable
- http_get_aspect_chain_64/light: rps=13363.00, p95_us=93.83, stability=unstable
- http_get_aspect_chain_64/saturated: rps=110890.50, p95_us=1854.71, stability=stable
- http_post_echo_1k/light: rps=15006.58, p95_us=77.22, stability=stable
- http_post_echo_1k/saturated: rps=77565.67, p95_us=2097.84, stability=stable
- http_post_echo_64k/light: rps=7610.16, p95_us=131.72, stability=unstable
- http_post_echo_64k/saturated: rps=83044.68, p95_us=1920.28, stability=stable
- http_session_counter/light: rps=8462.96, p95_us=35.94, stability=unstable
- http_session_counter/saturated: rps=124547.50, p95_us=6987.50, stability=stable

## Notes

- This report intentionally uses Release build to avoid Debug-mode distortion in throughput and latency.
- Several light-pressure cells are marked unstable because variance is high in two repetitions; this is expected for short quick-profile runs.
- For publication-grade comparison, run full profile and increase repetitions.
