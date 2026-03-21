# Benchmark Result Snapshots

This directory stores machine-specific benchmark artifacts.

Current machine profile:

- CPU: `13th Gen Intel(R) Core(TM) i9-13900H`
- logical CPU count observed by the benchmark: `20`
- OS: `Linux 6.19.6-200.fc43.x86_64 x86_64`
- compiler: `GNU 15.2.1`

Latest baseline (supersedes previous smoke-only snapshot):

- [2026-03-21-i9-13900h-full-short.md](2026-03-21-i9-13900h-full-short.md)
- [2026-03-21-i9-13900h-full-short.json](2026-03-21-i9-13900h-full-short.json)

Baseline run configuration:

- profile: `full`
- scenario set: `all`
- pressure set: `profile-default` (`light`, `balanced`, `saturated`, `overload`)
- warmup/duration/cooldown: `100ms / 200ms / 50ms`
- repetitions: `1`
- status: success (all cells completed)

Historical artifact (kept for reference):

- [2026-03-21-i9-13900h-smoke-light-short.md](2026-03-21-i9-13900h-smoke-light-short.md)
- [2026-03-21-i9-13900h-smoke-light-short.json](2026-03-21-i9-13900h-smoke-light-short.json)

Reproduce this baseline with:

```bash
./build-fix-check/benchmarks/bsrvcore_http_benchmark \
  --profile full \
  --warmup-ms 100 \
  --duration-ms 200 \
  --cooldown-ms 50 \
  --repetitions 1 \
  --output-json docs/benchmark-results/2026-03-21-i9-13900h-full-short.json
```
