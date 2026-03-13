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

## Build and Install

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
sudo cmake --install build
```

## Quick Start

Source: [examples/example_quick_start.cc](examples/example_quick_start.cc)

```cpp
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>

int main() {
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
- Learn aspects (middleware style): [docs/manual/aspects.md](docs/manual/aspects.md)
- Learn sessions/context: [docs/manual/sessions-context-cookies.md](docs/manual/sessions-context-cookies.md)
- Client tasks (HTTP/HTTPS + SSE): [docs/manual/client-tasks.md](docs/manual/client-tasks.md)

## Examples

The `examples/` folder contains runnable programs:

- See [docs/manual/examples.md](docs/manual/examples.md)

## Testing

- See [docs/manual/testing.md](docs/manual/testing.md)
