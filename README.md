# bsrvcore

bsrvcore is a C++ HTTP server library built on Boost.Asio and Boost.Beast.

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

## Core Concepts

### Configuration defaults

Source: [examples/example_configuration.cc](examples/example_configuration.cc)

```cpp
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>

int main() {
    auto server = std::make_unique<bsrvcore::HttpServer>(2);
    server
            ->SetDefaultReadExpiry(5000)
            ->SetDefaultWriteExpiry(5000)
            ->SetDefaultMaxBodySize(1024 * 1024)
            ->SetKeepAliveTimeout(15000)
            ->SetDefaultSessionTimeout(10 * 60 * 1000)
            ->SetSessionCleaner(true)
            ->AddRouteEntry(
                    bsrvcore::HttpRequestMethod::kGet, "/config",
                    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->GetResponse().result(boost::beast::http::status::ok);
                        task->SetField(boost::beast::http::field::content_type,
                                                     "text/plain; charset=utf-8");
                        task->SetBody("Default limits and timeouts are configured.\n");
                    })
            ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8081});

    if (!server->Start(1)) {
        std::cerr << "Failed to start server." << std::endl;
        return 1;
    }

    std::cout << "Listening on http://0.0.0.0:8081/config" << std::endl;
    std::cout << "Press Enter to stop." << std::endl;
    std::cin.get();

    server->Stop();
    return 0;
}
```

### OOP request handler and path parameters

Source: [examples/example_oop_handler.cc](examples/example_oop_handler.cc)

```cpp
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>
#include <string>

class HelloHandler : public bsrvcore::HttpRequestHandler {
 public:
    void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
        const auto& params = task->GetPathParameters();
        std::string name = params.empty() ? "world" : params.front();

        task->GetResponse().result(boost::beast::http::status::ok);
        task->SetField(boost::beast::http::field::content_type,
                                     "text/plain; charset=utf-8");
        task->SetBody("Hello, " + name + ".");
    }
};

int main() {
    auto server = std::make_unique<bsrvcore::HttpServer>(2);
    server
            ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/hello/{name}",
                                            std::make_unique<HelloHandler>())
            ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8082});

    if (!server->Start(1)) {
        std::cerr << "Failed to start server." << std::endl;
        return 1;
    }

    std::cout << "Listening on http://0.0.0.0:8082/hello/{name}" << std::endl;
    std::cout << "Press Enter to stop." << std::endl;
    std::cin.get();

    server->Stop();
    return 0;
}
```

### Aspects (pre/post hooks)

Source: [examples/example_aspect_basic.cc](examples/example_aspect_basic.cc)

```cpp
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>

int main() {
    auto server = std::make_unique<bsrvcore::HttpServer>(2);
    server
            ->AddGlobalAspect(
                    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetField("X-Request-Start", "1");
                    },
                    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetField("X-Request-End", "1");
                    })
            ->AddRouteEntry(
                    bsrvcore::HttpRequestMethod::kGet, "/ping",
                    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->GetResponse().result(boost::beast::http::status::ok);
                        task->SetField(boost::beast::http::field::content_type,
                                                     "text/plain; charset=utf-8");
                        task->SetBody("pong");
                    })
            ->AddAspect(
                    bsrvcore::HttpRequestMethod::kGet, "/ping",
                    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetField("X-Route-Aspect", "pre");
                    },
                    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetField("X-Route-Aspect", "post");
                    })
            ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8083});

    if (!server->Start(1)) {
        std::cerr << "Failed to start server." << std::endl;
        return 1;
    }

    std::cout << "Listening on http://0.0.0.0:8083/ping" << std::endl;
    std::cout << "Press Enter to stop." << std::endl;
    std::cin.get();

    server->Stop();
    return 0;
}
```

### Custom logger

Source: [examples/example_logger_custom.cc](examples/example_logger_custom.cc)

```cpp
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>
#include <string>

class ConsoleLogger : public bsrvcore::Logger {
 public:
    void Log(bsrvcore::LogLevel level, std::string message) override {
        std::clog << "[" << LevelToString(level) << "] " << message << std::endl;
    }

 private:
    const char* LevelToString(bsrvcore::LogLevel level) {
        switch (level) {
            case bsrvcore::LogLevel::kTrace:
                return "TRACE";
            case bsrvcore::LogLevel::kDebug:
                return "DEBUG";
            case bsrvcore::LogLevel::kInfo:
                return "INFO";
            case bsrvcore::LogLevel::kWarn:
                return "WARN";
            case bsrvcore::LogLevel::kError:
                return "ERROR";
            case bsrvcore::LogLevel::kFatal:
                return "FATAL";
            default:
                return "UNKNOWN";
        }
    }
};

int main() {
    auto server = std::make_unique<bsrvcore::HttpServer>(2);
    auto logger = std::make_shared<ConsoleLogger>();

    server
            ->SetLogger(logger)
            ->AddRouteEntry(
                    bsrvcore::HttpRequestMethod::kGet, "/log",
                    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->Log(bsrvcore::LogLevel::kInfo, "Handling /log");
                        task->GetResponse().result(boost::beast::http::status::ok);
                        task->SetField(boost::beast::http::field::content_type,
                                                     "text/plain; charset=utf-8");
                        task->SetBody("Logged a message.\n");
                    })
            ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8084});

    if (!server->Start(1)) {
        std::cerr << "Failed to start server." << std::endl;
        return 1;
    }

    logger->Log(bsrvcore::LogLevel::kInfo, "Listening on /log");
    std::cout << "Listening on http://0.0.0.0:8084/log" << std::endl;
    std::cout << "Press Enter to stop." << std::endl;
    std::cin.get();

    server->Stop();
    return 0;
}
```

### Session and Context

Source: [examples/example_session_context.cc](examples/example_session_context.cc)

```cpp
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <memory>
#include <string>

class UserAttribute : public bsrvcore::CloneableAttribute<UserAttribute> {
 public:
    explicit UserAttribute(std::string name) : name_(std::move(name)) {}

    std::string ToString() const override { return name_; }

    std::string name_;
};

int main() {
    auto server = std::make_unique<bsrvcore::HttpServer>(2);
    server
            ->AddRouteEntry(
                    bsrvcore::HttpRequestMethod::kGet, "/session",
                    [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        const std::string& session_id = task->GetSessionId();
                        auto session = task->GetSession();

                        if (session && !session->HasAttribute("user")) {
                            session->SetAttribute("user",
                                                                        std::make_shared<UserAttribute>("guest"));
                        }

                        std::string user_name = "unknown";
                        if (session) {
                            auto attr = session->GetAttribute("user");
                            auto user = std::dynamic_pointer_cast<UserAttribute>(attr);
                            if (user) {
                                user_name = user->ToString();
                            }
                        }

                        task->GetResponse().result(boost::beast::http::status::ok);
                        task->SetField(boost::beast::http::field::content_type,
                                                     "text/plain; charset=utf-8");
                        task->SetBody("sessionId=" + session_id + "\nuser=" + user_name +
                                                    "\n");
                    })
            ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8085});

    if (!server->Start(1)) {
        std::cerr << "Failed to start server." << std::endl;
        return 1;
    }

    std::cout << "Listening on http://0.0.0.0:8085/session" << std::endl;
    std::cout << "Press Enter to stop." << std::endl;
    std::cin.get();

    server->Stop();
    return 0;
}
```

## Build and Run Examples

Configure and build examples (default is ON in this repo):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBSRVCORE_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

Run examples:

```bash
./build/examples/example_quick_start
./build/examples/example_configuration
./build/examples/example_oop_handler
./build/examples/example_aspect_basic
./build/examples/example_logger_custom
./build/examples/example_session_context
```

To disable examples for consumers:

```bash
cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=OFF
```

## Common pitfalls / FAQ

- HTTPS: configure a TLS context with `SetSslContext` and provide valid
    certificates. The examples use plain HTTP.
- Logging: if you want output, provide a `Logger` via `SetLogger`.
