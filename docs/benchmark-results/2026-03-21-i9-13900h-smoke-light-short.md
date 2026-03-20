# Smoke Benchmark Snapshot (Light Pressure): 2026-03-21

This snapshot was collected on:

- CPU: `13th Gen Intel(R) Core(TM) i9-13900H`
- logical CPU count used by the benchmark presets: `20`
- OS: `Linux 6.19.6-200.fc43.x86_64 x86_64`
- compiler: `GNU 15.2.1`
- build type: `Release`

Execution sequence:

```bash
# 1) requested full run first (failed on light pressure timeout)
./build-bench-check/benchmarks/bsrvcore_http_benchmark \
  --profile full \
  --output-json /tmp/bsrvcore-benchmark-full-2026-03-21.json

# 2) short full smoke with all pressures (failed on saturated timeout)
./build-bench-check/benchmarks/bsrvcore_http_benchmark \
  --profile full \
  --warmup-ms 100 \
  --duration-ms 200 \
  --cooldown-ms 50 \
  --repetitions 1 \
  --output-json docs/benchmark-results/2026-03-21-i9-13900h-smoke-full-short.json

# 3) fallback smoke (completed and used as final snapshot)
./build-bench-check/benchmarks/bsrvcore_http_benchmark \
  --profile full \
  --pressure light \
  --warmup-ms 100 \
  --duration-ms 200 \
  --cooldown-ms 50 \
  --repetitions 1 \
  --output-json docs/benchmark-results/2026-03-21-i9-13900h-smoke-light-short.json
```

This is a **light-pressure smoke** snapshot only:

- it covers all 6 scenarios under `light` pressure (6 cells)
- it is suitable for quick functional regression comparison under minimal load
- it is **not** the long-duration official baseline

## Table

| Scenario | Pressure | Median RPS | Median MiB/s | Median P95 us |
| --- | --- | ---: | ---: | ---: |
| `http_get_static` | `light` | 11574 | 1.258 | 161.000 |
| `http_get_route_param` | `light` | 14327 | 1.679 | 144.000 |
| `http_get_global_aspect` | `light` | 13524 | 1.470 | 115.000 |
| `http_post_echo_1k` | `light` | 10066 | 11.395 | 148.000 |
| `http_post_echo_64k` | `light` | 813 | 50.962 | 1950.000 |
| `http_session_counter` | `light` | 11030 | 1.820 | 193.400 |
