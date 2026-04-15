# C Bindings And Packaging

This note explains why the C binding is implemented as a separate shared
library and how that choice interacts with the package split.

## Goals

The current C binding design has to satisfy all of these constraints at the
same time:

- keep the C ABI in its own public header tree under `include/bsrvcore-c/`
- keep the C package metadata in its own `c_devel` package
- avoid making `c_devel` depend on the C++ `devel` package
- keep the runtime package count small
- avoid changing the existing C++ server implementation model

These constraints are stronger than a simple "wrap the API with `extern \"C\"`"
goal, because package layout becomes part of the design.

## Source Layout

The C binding lives in its own small bridge layer:

- public header: [`include/bsrvcore-c/bsrvcore.h`](../../include/bsrvcore-c/bsrvcore.h)
- adapters and ABI bridge:
  [`src/c_binding/callback_adapters.cc`](../../src/c_binding/callback_adapters.cc),
  [`src/c_binding/server.cc`](../../src/c_binding/server.cc),
  [`src/c_binding/task.cc`](../../src/c_binding/task.cc)
- internal helpers:
  [`src/c_binding/include/internal/common.h`](../../src/c_binding/include/internal/common.h),
  [`src/c_binding/include/internal/callback_adapters.h`](../../src/c_binding/include/internal/callback_adapters.h)

This layer is intentionally thin:

- it owns ABI-safe wrapper structs
- it converts C enums and pointers into C++ calls
- it catches exceptions at the C ABI boundary
- it does not replace the underlying `HttpServer` / task lifecycle

## Runtime Model

The installed runtime currently contains two shared libraries:

- `libbsrvcore.so`
- `libbsrvcore-c.so`

`libbsrvcore-c.so` is not a second implementation of the server. It is a thin
wrapper library that links to `bsrvcore` and exports the standalone C ABI.

This keeps the runtime package count low:

- there is still only one runtime package
- but that runtime package may ship multiple `.so` files

## Why The C Binding Is Not Merged Into libbsrvcore.so

At first glance, merging the C ABI into `libbsrvcore.so` looks simpler.
Technically, it works. Packaging-wise, it breaks one of the project
requirements.

If the C ABI symbols are exported directly from `libbsrvcore.so`, then the C
development package has only two options:

1. depend on the C++ `devel` package, because that package owns the imported
   target / export metadata for `libbsrvcore.so`
2. duplicate the C++ package metadata in the C package, which makes ownership
   blurry and causes the C and C++ development packages to stop being cleanly
   separated

The project requirement is stricter:

- C and C++ development files must stay aligned
- but they must not depend on each other

The separate `libbsrvcore-c.so` solves this cleanly:

- `c_devel` owns the C header tree
- `c_devel` owns the `bsrvcore_c` CMake config and `pkg-config` file
- those files point to `libbsrvcore-c.so`
- `devel` continues to own only the C++ headers and C++ package metadata

That is why the current design keeps a separate C wrapper library even though
both libraries end up in the same runtime package.

## Static Build Constraint

Standalone static C binding consumption is intentionally not supported.

Reason:

- the only static archive for the wrapper model would need either a dedicated
  `libbsrvcore-c.a` or a dependency on the C++ static archive
- once `c_devel` depends on the C++ `devel` package, the packaging separation
  goal is lost

So the supported standalone shape is:

- shared runtime
- independent `c_devel`

## Build And Install Ownership

Current ownership split:

- `runtime`:
  - `libbsrvcore.so`
  - `libbsrvcore-c.so`
  - runtime executables such as `bsrvrun`
- `devel`:
  - `include/bsrvcore/`
  - `bsrvcoreConfig.cmake`
  - `bsrvcoreTargets.cmake`
  - `bsrvcore.pc`
- `c_devel`:
  - `include/bsrvcore-c/`
  - `bsrvcore_cConfig.cmake`
  - `bsrvcore_cTargets.cmake`
  - `bsrvcore-c.pc`

This split keeps the ownership rule simple: each development package owns only
its own headers and consumer metadata.

## Design Rule

When changing the C binding in future, preserve these invariants unless the
package policy is intentionally changed:

- no C header in `include/bsrvcore/`
- no C binding metadata in `devel`
- no C++ metadata requirement from `c_devel`
- no C ABI symbols added directly to `libbsrvcore.so` unless package ownership
  rules are redesigned at the same time
