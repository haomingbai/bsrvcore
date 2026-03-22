# Linux I/O Model Choice: epoll vs io_uring

This chapter explains why `bsrvcore` currently prefers `epoll` as the default
Linux backend, even though `io_uring` exists.

## Short Answer

`io_uring` can be faster for some workloads, but "faster in theory" is not the
same as "faster and safer as a default in production". The default must work
well across mainstream Linux distributions, limits, and deployment policies.

## Asio-Level Reason

`bsrvcore` uses Boost.Asio. In this stack:

- `epoll` is the long-standing, broadly exercised backend on Linux
- Asio `io_uring` support is newer and controlled by compile-time macros
- backend choice can interact with build-time ABI/ODR assumptions if mixed
  across components

So operationally, `epoll` is the lower-risk default unless a project validates
`io_uring` under its exact build and runtime constraints.

## MEMLOCK Limit Reason (Mainstream Distros Included)

`io_uring` setup depends on locked memory budget (`RLIMIT_MEMLOCK`) for ring
resources.

In real environments (including mainstream Fedora installs and many systemd
service defaults), this limit is often conservative. The result can be:

- ring initialization failure (`Cannot allocate memory`)
- forced use of smaller rings than ideal
- unstable tail latency under load

`epoll` does not have this specific locked-memory startup sensitivity.

## Contention and Workload-Match Reason

`io_uring` gains are workload-dependent. It tends to shine when the workload is
strongly syscall-bound and tuned for `io_uring` features.

Typical HTTP server paths often include substantial non-I/O cost:

- request parsing
- routing
- user handler logic
- memory management

When these dominate, backend switching alone may not improve throughput, and
can even regress due to queue management and synchronization overhead.

## Feature-Tuning Gap Reason

To get strong `io_uring` gains, teams often need deeper tuning, such as:

- ring sizing strategy per thread/process
- fixed buffer/file usage policies
- polling mode and CPU policy choices
- runtime limit tuning (`MEMLOCK`, cgroup/systemd settings)

Without this end-to-end tuning, `io_uring` frequently under-delivers compared
to its best-case expectations.

## Operational Default Policy

For `bsrvcore`, the default policy is:

- use `epoll` as the safe baseline on Linux
- allow explicit `io_uring` experiments in controlled builds
- switch defaults only after workload-specific validation shows net gain under
  target deployment limits

This policy optimizes for predictable behavior first, and peak performance
second.
