# bsrvcore Testing Architecture and Plan

This document describes the test strategy, coverage, and how to run tests.
It is based on the current repository implementation (public headers + internal sources).

## Goals

- Validate core public APIs: correctness, edge cases, error propagation, and lifecycle behavior.
- Validate key modules (routing, aspects/AOP, sessions, context), including thread-safety where required.
- Keep the test suite stable, reproducible, and runnable in both CI and local development.
- Provide stress/concurrency/long-run tests with controllable parameters and useful diagnostics.

## CI layout

- [`.github/workflows/ci-test.yml`](../.github/workflows/ci-test.yml) currently runs Linux and Windows test matrix jobs for `unit`, `integration`, and `stress` labels.
- macOS CI steps are intentionally kept as commented blocks in the workflow and are not active right now.
- [`.github/workflows/ci-package.yml`](../.github/workflows/ci-package.yml) is Linux-only and produces the release packages for the supported glibc-based distribution targets.
- Release upload only collects final package files from the build tree. CPack internal files such as `control.tar.gz` and `data.tar.gz` are intentionally excluded.

## Compiler support policy

- Current project support baseline is LLVM toolchains (Clang/LLVM).
- Non-LLVM toolchains are not considered supported at this stage.
- This policy may be expanded in a future update when toolchain behavior is stable enough for CI coverage.

## Coverage by test type

### 1) Unit tests

| Module | Purpose | Key assertions / edges | Notes |
| --- | --- | --- | --- |
| `Context` | Thread-safe key-value container | `Set`/`Get`/`Has` correctness; concurrent reads/writes | Uses public headers only |
| `Attribute` / `CloneableAttribute` | Polymorphic attribute semantics | deep-copy via `Clone`; default `Type`/`Equals`/`Hash` behavior | Uses public headers only |
| `ServerSetCookie` | `Set-Cookie` generation | missing name/value returns empty; correct combinations of `SameSite`/`HttpOnly`/`Secure`/`Max-Age`/`Path`/`Domain` | Uses public headers only |
| Routing behavior | Route matching and parameter extraction | parameter routes vs exclusive routes vs invalid route registration | Verified through public `HttpServer` acceptance tests |
| `HttpRequestHandler` / `FunctionRouteHandler` | Exception handling and logging path | handler exceptions do not crash the server path | Verify logger calls with a mock logger (gmock) |

### 2) Integration tests

| Scenario | Purpose | Key assertions | Notes |
| --- | --- | --- | --- |
| Minimal HTTP server | End-to-end routing | GET/POST responses; body/headers correctness | Uses Boost.Beast client in the same process |
| Aspect order (AOP) | Pre/Post ordering rules | global/method/route aspect order; reverse order for post-aspects | Mark order in response body and assert |
| Session and cookies | session id generation and write-back | missing cookie generates a session id and emits `Set-Cookie` | Capture response headers from the test connection |

### 3) Stress tests (concurrency correctness / deadlock detection / long-run)

| Scenario | Purpose | Assertions / thresholds | Parameters |
| --- | --- | --- | --- |
| High-concurrency `Context` reads/writes | Validate locks and data consistency | no deadlocks; final counters match | `BSRVCORE_STRESS_THREADS`, `BSRVCORE_STRESS_ITERATIONS`, `BSRVCORE_STRESS_SEED` |
| `HttpServer::Post` task flood | Reliability under heavy concurrent posting | all tasks complete; timeout fails the test | `BSRVCORE_STRESS_THREADS`, `BSRVCORE_STRESS_ITERATIONS`, `BSRVCORE_STRESS_SEED` |
| End-to-end concurrent request safety | Catch deadlocks/livelocks and protocol correctness issues | all requests complete correctly within timeout | `BSRVCORE_STRESS_ITERATIONS`, `BSRVCORE_STRESS_TIMEOUT_MS` |

Stress tests are **OFF by default**. They are built and executed only when `BSRVCORE_ENABLE_STRESS_TESTS=ON`.

## Clear boundaries

- **Unit**: no real network; no real server; minimal threading (or none).
- **Integration**: start `HttpServer` and perform real HTTP round-trips via loopback.
- **Stress**: focus on concurrency correctness and deadlock/livelock detection; always has timeout and reproducible randomness.

## Key invariants and edge cases

- Route parameters must be extracted correctly, and exclusive routes must not be confused with parameter routes.
- `HttpServer` configuration must not be modified while the server is running (covered by existing tests).
- Cookie generation must handle missing fields; `SameSite=None` must imply `Secure`.
- Session id generation must be stable and written back via `Set-Cookie`.
- Exception handling: exceptions from `FunctionRouteHandler` must be swallowed and logged.

## Reproducibility and diagnostics

- Stress tests use a fixed default seed (a constant), and allow overrides via environment variables.
- On failure, tests should print the seed, thread count, iteration count, and the last operation index.
- All concurrency-focused tests have timeouts to avoid hanging forever, and should report clear error messages.

## How to run

### Local build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBSRVCORE_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Run only one group (by label)

```bash
ctest --test-dir build -L unit
ctest --test-dir build -L integration
ctest --test-dir build -L stress
```

### Enable stress tests

```bash
cmake -S . -B build -DBSRVCORE_BUILD_TESTS=ON -DBSRVCORE_ENABLE_STRESS_TESTS=ON
cmake --build build --parallel
ctest --test-dir build -L stress --output-on-failure
```

### Stress test knobs (environment variables)

- `BSRVCORE_STRESS_THREADS`: number of threads (default: 8)
- `BSRVCORE_STRESS_ITERATIONS`: number of iterations (module-specific defaults)
- `BSRVCORE_STRESS_SEED`: random seed (default: 1337)
- `BSRVCORE_STRESS_TIMEOUT_MS`: timeout in milliseconds (default: 120000)

## Runtime note

- Unit and integration tests are expected to be fast for normal development loops.
- Stress tests are not benchmark targets; they may run longer by design due to
  generous timeouts for deadlock/livelock detection.

## Sanitizers and coverage

- ASan/UBSan/TSan: recommended for `RelWithDebInfo` or `Debug` builds.
- Coverage: if enabled later, configure it in CI as a separate job; it is not a required default step.

## Test visibility and dependencies

- Most tests use public headers only.
- Routing and session behavior are tested through public `HttpServer` and `HttpServerTask` APIs only.
- Tests no longer require `src/include/bsrvcore/internal` visibility.
- Public APIs are not changed for tests. If test hooks become necessary later, they must be behind a build-time switch and recorded in this document.
