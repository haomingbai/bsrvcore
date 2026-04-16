# bsrvcore

bsrvcore is a C++ HTTP server library built on Boost.Asio and Boost.Beast.

It focuses on a clean "learning-friendly" API:

- Routing with path parameters
- Aspects (before/after hooks, like middleware)
- Sessions + request context
- Optional TLS (HTTPS)
- Optional client tasks (HTTP/HTTPS + SSE)

Tutorial-style manual (recommended):

- See [docs/manual/index.md](docs/manual/index.md)

Maintainer / internals notes:

- See [docs/design/index.md](docs/design/index.md)

API reference:

- This project relies on Doxygen generated from public headers under `include/`.

## Requirements

- C++20 compiler
- CMake 3.25+
- Boost (components: headers, system, url, json, program_options)
- OpenSSL

## Toolchain Support

- The project currently targets LLVM toolchains (Clang/LLVM) as the supported baseline.
- Non-LLVM toolchains are not considered officially supported at this stage.
- Support scope may expand in future releases when compatibility and CI coverage are ready.

## Build and Install

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
sudo cmake --install build
```

Library type is controlled by `BUILD_SHARED_LIBS`:

- Shared library (default):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
```

- Static library:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
```

As a subproject (`add_subdirectory`), set `BUILD_SHARED_LIBS` in the parent project
before adding `bsrvcore`:

```cmake
# Parent project's CMakeLists.txt
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libs across subprojects" FORCE)
add_subdirectory(external/bsrvcore)

add_executable(app main.cc)
target_link_libraries(app PRIVATE bsrvcore::bsrvcore)
```

Compile a standalone program against installed `bsrvcore`:

```bash
g++ a.cpp -std=c++20 $(pkg-config --cflags --libs bsrvcore)
```

For C applications, enable the C bindings and include the standalone header:

```c
#include <bsrvcore-c/bsrvcore.h>
```

Recommended CMake consumption:

```cmake
find_package(bsrvcore_c CONFIG REQUIRED)

add_executable(app main.c)
target_link_libraries(app PRIVATE bsrvcore::bsrvcore_c)
```

The C package links against the standalone wrapper library `libbsrvcore-c.so`.

Direct `pkg-config` use also works:

```bash
cc main.c $(pkg-config --cflags --libs bsrvcore-c)
```

See [docs/manual/c-bindings.md](docs/manual/c-bindings.md) for the full C API guide,
package layout, stateful handlers, aspects, and request/response rules.

If you install shared libraries manually (without `dnf/apt/rpm/dpkg`), refresh the
dynamic linker cache once:

```bash
sudo ldconfig
```

## Quick Start

Source: [examples/getting-started/quick_start.cc](examples/getting-started/quick_start.cc)

The public API now exposes common Asio / Beast / JSON concepts through
`include/bsrvcore/core/types.h`, and `bsrvcore/bsrvcore.h` re-exports those
aliases. Prefer names such as `bsrvcore::IoExecutor`, `bsrvcore::SslContext`,
`bsrvcore::HttpField`, and `bsrvcore::HttpStatus` in application code.

```cpp
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>

#include <iostream>
#include <memory>

int main() {
    // Worker threads for Post/SetTimer callbacks.
    auto server = std::make_unique<bsrvcore::HttpServer>(4);
    server
            ->AddRouteEntry(
                    bsrvcore::HttpRequestMethod::kGet, "/hello",
                    [](const std::shared_ptr<bsrvcore::HttpServerTask>& task) {
                        task->GetResponse().result(bsrvcore::HttpStatus::ok);
                        task->SetField(bsrvcore::HttpField::content_type,
                                                     "text/plain; charset=utf-8");
                        task->SetBody("Hello, bsrvcore.");
                    })
                ->AddListen(
                    bsrvcore::TcpEndpoint(boost::asio::ip::make_address("0.0.0.0"),
                                          8080),
                    2);

              if (!server->Start()) {
        std::cerr << "Failed to start server." << std::endl;
        return 1;
    }

    std::cout << "Listening on http://0.0.0.0:8080/hello" << std::endl;
    std::cout << "Press Enter to stop." << std::endl;
    std::cin.get();

    server->Stop();
    return 0;
}
```

## C Binding Quick Start

Source: [examples/c-binding/quick_start.c](examples/c-binding/quick_start.c)

```c
#include <bsrvcore-c/bsrvcore.h>

#include <stdio.h>

static void hello_handler(bsrvcore_http_server_task_t* task) {
  bsrvcore_http_server_task_set_response(
      task, 200, "text/plain; charset=utf-8", "Hello, bsrvcore C binding.\n",
      sizeof("Hello, bsrvcore C binding.\n") - 1);
}

int main(void) {
  bsrvcore_server_t* server = NULL;

  if (bsrvcore_server_create(4, &server) != BSRVCORE_RESULT_OK) {
    return 1;
  }

  if (bsrvcore_server_add_route(server, BSRVCORE_HTTP_METHOD_GET, "/hello",
                                hello_handler) != BSRVCORE_RESULT_OK ||
      bsrvcore_server_add_listen(server, "0.0.0.0", 8080, 2) !=
          BSRVCORE_RESULT_OK ||
      bsrvcore_server_start(server) != BSRVCORE_RESULT_OK) {
    bsrvcore_server_destroy(server);
    return 1;
  }

  puts("Listening on http://0.0.0.0:8080/hello");
  (void)getchar();

  (void)bsrvcore_server_stop(server);
  bsrvcore_server_destroy(server);
  return 0;
}
```

## Upgrade Information

### API Change (v0.16.0+)

Handler and aspect virtual functions now accept `const std::shared_ptr<T>&`
instead of `std::shared_ptr<T>` by value. This is a **breaking change** that
requires:

1. **Recompilation required** for all code using custom handler/aspect classes.
2. **Code updates** only if you define custom handlers or aspects:

   ```cpp
   // OLD (v0.14.x)
   class MyHandler : public HttpRequestHandler {
     void Service(std::shared_ptr<HttpServerTask> task) override { ... }
   };

   // NEW (v0.16.0+)
   class MyHandler : public HttpRequestHandler {
     void Service(const std::shared_ptr<HttpServerTask>& task) override { ... }
   };
   ```

3. **Lambda handlers** (in examples and application code) auto-adapt—no changes
   needed.

**Benefit**: ~5-15% throughput improvement in typical request paths through
reduced atomic reference-count operations. See [IO Thread Optimizations in the
design docs](docs/design/request-lifecycle.md#io-thread-optimizations-via-const-ref-handler-parameters).

## Next steps (tutorial)

- Start here: [docs/manual/getting-started.md](docs/manual/getting-started.md)
- Use the C API: [docs/manual/c-bindings.md](docs/manual/c-bindings.md)
- Learn routing: [docs/manual/routing.md](docs/manual/routing.md)
- Process multipart and PUT bodies: [docs/manual/request-body-processing.md](docs/manual/request-body-processing.md)
- Serve SSE streams: [docs/manual/sse-server.md](docs/manual/sse-server.md)
- Learn aspects (middleware style): [docs/manual/aspects.md](docs/manual/aspects.md)
- Learn sessions/context: [docs/manual/sessions-context-cookies.md](docs/manual/sessions-context-cookies.md)
- Client tasks (HTTP/HTTPS + SSE): [docs/manual/client-tasks.md](docs/manual/client-tasks.md)

## Optional Extensions

- OAI chat completion has moved to the standalone `boai` library.
- `bsrvcore` now stays focused on the reusable server/client foundation that
  other libraries can depend on.

## bsrvrun (runtime executable)

`bsrvrun` is installed in the runtime package and starts a server from YAML.

Config path resolution order:

1. `-c/--config <path>`
2. `./bsrvrun.yaml`
3. `/etc/bsrvrun/bsrvrun.yaml`

Run:

```bash
bsrvrun -c ./bsrvrun.yaml
```

For full schema and plugin ABI contract, see:

- [docs/manual/bsrvrun.md](docs/manual/bsrvrun.md)

## Examples

The `examples/` folder contains runnable programs:

- `examples/getting-started/`
- `examples/c-binding/`
- `examples/http-server/`
- `examples/sse/`
- `examples/routing/`
- `examples/aspects/`
- `examples/sessions-context/`
- `examples/logging/`
- `examples/client-tasks/`
- `examples/websocket-tasks/`
- `examples/bsrvrun/`

- See [docs/manual/examples.md](docs/manual/examples.md)

## Testing

- See [docs/manual/testing.md](docs/manual/testing.md)

## Formatting

Format the repository with `clang-format`:

```bash
./scripts/format.sh
```

Use IWYU to remove unused includes and normalize include blocks:

```bash
./scripts/iwyu.sh --dry-run
./scripts/iwyu.sh
```

The IWYU script reuses an existing `compile_commands.json` when available and
otherwise configures a build directory automatically. If IWYU is not installed,
it prints a message and exits without changing files.

## Benchmarking

Build the standalone benchmark suite:

```bash
cmake -S . -B build-bench \
  -DCMAKE_BUILD_TYPE=Release \
  -DBSRVCORE_BUILD_EXAMPLES=OFF \
  -DBSRVCORE_BUILD_TESTS=OFF \
  -DBSRVCORE_BUILD_BENCHMARKS=ON
cmake --build build-bench --target bsrvcore_http_benchmark --parallel
```

Run a short sweep:

```bash
./build-bench/benchmarks/bsrvcore_http_benchmark --profile quick --wrk-bin /usr/bin/wrk
```

Notes:

- The repository does not ship `wrk` binaries by default.
- The benchmark probes `/bin/wrk`, `/usr/bin/wrk`, `/usr/local/bin/wrk`, then `PATH`.
- You can enable bundled build explicitly with
  `-DBSRVCORE_BENCHMARK_BUILD_BUNDLED_WRK=ON`.

For benchmark methodology and shipped snapshots, see:

- [docs/manual/benchmarking.md](docs/manual/benchmarking.md)
- [docs/manual/linux-io-model-choice.md](docs/manual/linux-io-model-choice.md)
- [docs/benchmark-results/benchmark-report.md](docs/benchmark-results/benchmark-report.md)
