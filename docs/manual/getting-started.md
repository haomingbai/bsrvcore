# Getting started

## What is bsrvcore?

bsrvcore is a C++ HTTP server library built on Boost.Asio and Boost.Beast.

It also provides client-side helper tasks:

- `HttpClientTask` for HTTP/HTTPS requests
- `HttpSseClientTask` for Server-Sent Events (SSE)

If you want to use bsrvcore from C instead of C++, read
[C bindings](c-bindings.md).

## Requirements

- C++20 compiler
- CMake 3.25+
- Boost: `headers`, `system`, `url`, `json`, `program_options`
- OpenSSL (for HTTPS)

## Build

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Install

```bash
sudo cmake --install build
```

## Core type aliases

`bsrvcore/core/types.h` centralizes the Boost.Asio / Beast / JSON concepts that
show up most often in bsrvcore APIs, such as `bsrvcore::IoContext`,
`bsrvcore::IoExecutor`, `bsrvcore::SslContext`, `bsrvcore::HttpField`,
`bsrvcore::HttpStatus`, and the JSON aliases. `bsrvcore/bsrvcore.h` re-exports
these aliases, so most applications do not need to include `types.h`
separately.

## Minimal server

```cpp
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>

#include <iostream>
#include <memory>

int main() {
  auto server = std::make_unique<bsrvcore::HttpServer>(4);

  server->AddRouteEntry(
            bsrvcore::HttpRequestMethod::kGet,
            "/hello",
            [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
              task->GetResponse().result(bsrvcore::HttpStatus::ok);
              task->SetField(bsrvcore::HttpField::content_type,
                             "text/plain; charset=utf-8");
              task->SetBody("Hello, bsrvcore.\n");
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
  std::cin.get();
  server->Stop();
  return 0;
}
```

Next: read [HTTP server](http-server.md).
