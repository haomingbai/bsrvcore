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

API reference:

- This project relies on Doxygen generated from public headers under `include/bsrvcore/`.

## Requirements

- C++23 compiler
- CMake 3.25+
- Boost (components: system, url, asio, beast)
- OpenSSL
- liburing

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
g++ a.cpp -std=c++23 $(pkg-config --cflags --libs bsrvcore)
```

If you install shared libraries manually (without `dnf/apt/rpm/dpkg`), refresh the
dynamic linker cache once:

```bash
sudo ldconfig
```

## Quick Start

Source: [examples/getting-started/quick_start.cc](examples/getting-started/quick_start.cc)

```cpp
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>

int main() {
    // Worker threads for Post/SetTimer callbacks.
    auto server = std::make_unique<bsrvcore::HttpServer>(4);
    server
            ->AddRouteEntry(
                    bsrvcore::HttpRequestMethod::kGet, "/hello",
                    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->GetResponse().result(boost::beast::http::status::ok);
                        task->SetField(boost::beast::http::field::content_type,
                                                     "text/plain; charset=utf-8");
                        task->SetBody("Hello, bsrvcore.");
                    })
            ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8080});

    // I/O threads for accept/read/write.
    if (!server->Start(2)) {
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

## Next steps (tutorial)

- Start here: [docs/manual/getting-started.md](docs/manual/getting-started.md)
- Learn routing: [docs/manual/routing.md](docs/manual/routing.md)
- Process multipart and PUT bodies: [docs/manual/request-body-processing.md](docs/manual/request-body-processing.md)
- Serve SSE streams: [docs/manual/sse-server.md](docs/manual/sse-server.md)
- Learn aspects (middleware style): [docs/manual/aspects.md](docs/manual/aspects.md)
- Learn sessions/context: [docs/manual/sessions-context-cookies.md](docs/manual/sessions-context-cookies.md)
- Client tasks (HTTP/HTTPS + SSE): [docs/manual/client-tasks.md](docs/manual/client-tasks.md)

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
- `examples/http-server/`
- `examples/sse/`
- `examples/routing/`
- `examples/aspects/`
- `examples/sessions-context/`
- `examples/logging/`
- `examples/client-tasks/`
- `examples/bsrvrun/`

- See [docs/manual/examples.md](docs/manual/examples.md)

## Testing

- See [docs/manual/testing.md](docs/manual/testing.md)

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
