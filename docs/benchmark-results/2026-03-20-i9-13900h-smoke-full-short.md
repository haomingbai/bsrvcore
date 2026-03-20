# Smoke Benchmark Snapshot: 2026-03-20

This snapshot was collected on:

- CPU: `13th Gen Intel(R) Core(TM) i9-13900H`
- logical CPU count used by the benchmark presets: `20`
- OS: `Linux 6.19.6-200.fc43.x86_64 x86_64`
- compiler: `GNU 15.2.1`
- build type: `Release`

Command:

```bash
./build-bench-check/benchmarks/bsrvcore_http_benchmark \
  --profile full \
  --warmup-ms 500 \
  --duration-ms 1000 \
  --cooldown-ms 100 \
  --repetitions 1 \
  --output-json docs/benchmark-results/2026-03-20-i9-13900h-smoke-full-short.json
```

This is a **smoke** snapshot only:

- it covers all 6 scenarios and all 4 pressure levels
- it is useful for a fast local baseline and for comparing obvious regressions
- it is **not** the final multi-round official baseline

Why it is not the final baseline:

- the default long-duration `quick` / `full` profiles still surface a library-side keep-alive timeout in long-lived single-connection runs for `http_get_route_param/light` and `http_session_counter/light`
- the benchmark harness now reports that failure correctly instead of hanging

## Short Reading

- Small-response GET paths peak around `90k-102k rps` on this machine once concurrency is high enough.
- The extra global aspect pair does not materially change short-run throughput relative to plain static GET.
- `POST /echo` with `1 KiB` bodies reaches roughly `82-92 MiB/s`, while `64 KiB` bodies trade request rate for much higher wire throughput, reaching about `374-400 MiB/s`.
- `64 KiB` bodies are the first scenario where tail latency grows sharply under saturation and overload.

## Table

| Scenario | Pressure | Median RPS | Median MiB/s | Median P95 us |
| --- | --- | ---: | ---: | ---: |
| `http_get_static` | `light` | 17819 | 1.94 | 101.00 |
| `http_get_static` | `balanced` | 100870 | 10.97 | 475.00 |
| `http_get_static` | `saturated` | 90445 | 9.83 | 2026.00 |
| `http_get_static` | `overload` | 90028 | 9.79 | 3987.00 |
| `http_get_route_param` | `light` | 25758 | 3.05 | 82.00 |
| `http_get_route_param` | `balanced` | 101894 | 12.03 | 472.00 |
| `http_get_route_param` | `saturated` | 89252 | 10.50 | 2052.00 |
| `http_get_route_param` | `overload` | 90110 | 10.63 | 3872.00 |
| `http_get_global_aspect` | `light` | 23730 | 2.58 | 87.00 |
| `http_get_global_aspect` | `balanced` | 99570 | 10.82 | 489.00 |
| `http_get_global_aspect` | `saturated` | 91461 | 9.94 | 2047.00 |
| `http_get_global_aspect` | `overload` | 90604 | 9.85 | 3967.00 |
| `http_post_echo_1k` | `light` | 25986 | 29.42 | 53.00 |
| `http_post_echo_1k` | `balanced` | 81679 | 92.46 | 584.00 |
| `http_post_echo_1k` | `saturated` | 72645 | 82.23 | 2445.00 |
| `http_post_echo_1k` | `overload` | 74287 | 84.09 | 4597.00 |
| `http_post_echo_64k` | `light` | 1117 | 69.98 | 1458.00 |
| `http_post_echo_64k` | `balanced` | 6392 | 400.48 | 6551.40 |
| `http_post_echo_64k` | `saturated` | 5976 | 374.45 | 28123.05 |
| `http_post_echo_64k` | `overload` | 5964 | 373.66 | 55142.10 |
| `http_session_counter` | `light` | 23017 | 3.80 | 89.00 |
| `http_session_counter` | `balanced` | 98187 | 16.20 | 490.00 |
| `http_session_counter` | `saturated` | 91880 | 15.16 | 2002.00 |
| `http_session_counter` | `overload` | 89062 | 14.69 | 3947.00 |
