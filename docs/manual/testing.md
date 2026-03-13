# Testing

This chapter explains how to build and run tests.

## Build tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBSRVCORE_BUILD_TESTS=ON
cmake --build build --parallel
```

## Run tests

```bash
ctest --test-dir build --output-on-failure
```

Run only a group by label:

```bash
ctest --test-dir build -L unit
ctest --test-dir build -L integration
ctest --test-dir build -L stress
```

## Stress tests

Some stress tests may be disabled unless enabled at configure time:

```bash
cmake -S . -B build -DBSRVCORE_BUILD_TESTS=ON -DBSRVCORE_ENABLE_STRESS_TESTS=ON
cmake --build build --parallel
ctest --test-dir build -L stress --output-on-failure
```

Common environment variables used by stress tests:

- `BSRVCORE_STRESS_THREADS`
- `BSRVCORE_STRESS_ITERATIONS`
- `BSRVCORE_STRESS_SEED`
- `BSRVCORE_STRESS_TIMEOUT_MS`

For more detailed testing architecture notes, see [../testing.md](../testing.md).
