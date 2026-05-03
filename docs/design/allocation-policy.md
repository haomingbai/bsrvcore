# Allocation Policy

bsrvcore has allocator-backed runtime types, but not every file should use
them explicitly.

## Library Runtime

Library internals and request/runtime hot paths should prefer:

- `AllocateUnique<T>()` and `AllocateShared<T>()`
- `OwnedPtr<T>` for allocator-aware unique ownership
- `AllocatedVector`, `AllocatedString`, `AllocatedStringMap`, and related
  allocator-backed containers

This applies to route tables, task lifecycle state, connection write queues,
session storage, client task state, and other framework-owned runtime objects.

## Examples And Ordinary Tests

Examples and ordinary behavior tests should prefer standard C++ allocation:

- stack objects where ownership does not need to escape
- `std::make_unique<T>()`
- `std::make_shared<T>()`

This keeps sample code focused on public API usage instead of internal allocator
mechanics.

## Allocator-Specific Tests

Allocator-focused tests, public allocator ABI tests, and tests that directly
exercise `OwnedPtr`/`AdoptUniqueAs` behavior may use the bsrvcore allocator
APIs explicitly.

## bsrvrun Plugins

The bsrvrun handler/aspect plugin factory ABI returns `OwnedPtr`. Plugin
examples and tests may construct ordinary C++ objects with `std::make_unique`
and return them via `AdoptUniqueAs(...)` so plugin code does not need to perform
custom allocator allocation itself.

Logger factories return `std::shared_ptr`, so plugin loggers should normally use
`std::make_shared`.

## Benchmarks

Benchmark server/scenario runtime paths should continue to use bsrvcore
allocator APIs so benchmark measurements represent production runtime behavior.

Benchmark reporting, plotting, packaging, and other offline tooling may use
standard containers and allocation.
