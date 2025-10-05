# bsrvcore: A simple web framework with support of RESTful API based on Boost.Asio and Boost.Beast

**bsrvcore** is a light-weighted net framework of Asio and Beast which is suitable for quick web back-end development with the help of several libraries like Boost.JSON, Boost.URL, Boost.Log and so on.
This project descrete the worker threads and IO threads which can optimize the performance when facing CPU heavy situations.

## Quick Start

**bsrvcore** supports multiple programming paradigms including Functional Programming, Object-Oriented Programming (OOP), and Aspect-Oriented Programming (AOP). Below are examples of how to create a simple "Hello World" HTTP server using each of these styles.

```cpp
#include <iostream>
#include <memory>
#include "bsrvcore/bsrvcore.h"

int main() {
    using namespace bsrvcore;

    // 1. Create a server instance
    auto server = std::make_unique<HttpServer>(4); // 4 worker threads

    // 2. Configure the server using a chained-method style
    server
        ->AddRouteEntry(
            HttpRequestMethod::kGet, "/hello/{name}",
            // Use a lambda for the handler logic
            [](std::shared_ptr<HttpServerTask> task) {
                // The actual logic of the handler
                task->Log(LogLevel::kInfo, "A request is received.");
                task->SetBody("Hello from the functional style handler!");
                // In a real app, you would extract '{name}' from the path here
            })
        ->AddRouteEntry(
            HttpRequestMethod::kGet, "/status",
            [](std::shared_ptr<HttpServerTask> task) {
                task->SetBody("{\"status\": \"ok\"}");
                // In a real app, you would set the content-type to
                // application/json
            })
        ->AddListen({{}, 8080}); // Listen on all interfaces, port 8080

    // 3. Start the server with 4 IO threads
    if (server->Start(4)) {
        std::cout << "The Hello World HTTP server is running."
                  << std::endl;
        std::cout << "Press Enter to stop the server..." << std::endl;
        getchar();
        server->Stop();
    } else {
        std::cerr << "Failed to start server." << std::endl;
    }

    return 0;
}
```

If you prefer a more traditional OOP style, you can also do this:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include "bsrvcore/bsrvcore.h"

// --- Handler Definitions ---

/**
 * @class UserHandler
 * @brief Handles requests related to user data.
 * @details This class encapsulates all logic for the /api/user/{id} endpoint.
 * It could have dependencies (like a database connection) injected into its
 * constructor.
 */
class UserHandler : public bsrvcore::HttpRequestHandler {
   public:
    // Override the `Service` method to implement the request handling logic.
    void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
        task->Log(bsrvcore::LogLevel::kInfo, "OOP UserHandler is executing.");
        // In a real application, you'd fetch user data based on an ID from the
        // path.
        task->SetBody("Response from UserHandler (OOP Style).");
    }
};

/**
 * @class SystemHealthHandler
 * @brief Handles requests for system health checks.
 */
class SystemHealthHandler : public bsrvcore::HttpRequestHandler {
   public:
    void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
        task->Log(bsrvcore::LogLevel::kInfo,
                  "OOP SystemHealthHandler is executing.");
        task->SetBody("System is healthy.");
    }
};


int main() {
    using namespace bsrvcore;

    // 1. Create a server instance
    auto server = std::make_unique<HttpServer>(4);

    // 2. Register instances of handler classes for specific routes
    server
        ->AddRouteEntry(HttpRequestMethod::kGet, "/api/user/{id}",
                        std::make_unique<UserHandler>())
        ->AddRouteEntry(HttpRequestMethod::kGet, "/health",
                        std::make_unique<SystemHealthHandler>())
        ->AddListen({{}, 8081}); // Listen on port 8081

    // 3. Start the server
    if (server->Start(4)) {
        std::cout << "An OOP style HTTP server is running." << std::endl;
        std::cout << "Press Enter to stop the server..." << std::endl;
        getchar();
        server->Stop();
    } else {
        std::cerr << "Failed to start server." << std::endl;
    }

    return 0;
}
```

If you prefer the advanced `AOP` style, you can also do this:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include "bsrvcore/bsrvcore.h"

// --- Aspect Definition (OOP Style) ---

/**
 * @class AuthAspect
 * @brief An aspect that simulates an authentication check.
 * @details It runs a `PreService` check before the main handler. If the check
 * fails, it stops further processing and sends a 401 Unauthorized response.
 */
class AuthAspect : public bsrvcore::HttpRequestAspectHandler {
   public:
    // This runs BEFORE the main route handler
    void PreService(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
        task->Log(bsrvcore::LogLevel::kInfo, "AuthAspect: Checking credentials...");
        // In a real app, you would check request headers for a token.
        // We'll simulate failure for demonstration.
        bool is_authenticated = false;

        if (!is_authenticated) {
            task->Log(bsrvcore::LogLevel::kWarn, "Auth failed!");
            task->SetResponse(401, "Unauthorized Access");
            task->StopProcessing(); // Prevents the main handler from running
        } else {
            task->Log(bsrvcore::LogLevel::kInfo, "Auth successful.");
        }
    }

    // This runs AFTER the main route handler (if it was executed)
    void PostService(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
        // This will only be called if PreService did not stop processing.
        task->Log(bsrvcore::LogLevel::kInfo, "AuthAspect: Post-processing complete.");
    }
};

// --- Handler Definition (OOP Style for demonstration) ---
class SecretDataHandler : public bsrvcore::HttpRequestHandler {
   public:
    void Service(std::shared_ptr<bsrvcore::HttpServerTask> task) override {
        // This code will NOT run if the AuthAspect fails the check.
        task->Log(bsrvcore::LogLevel::kInfo, "SecretDataHandler is executing.");
        task->SetBody("This is secret data. You should not see this without auth!");
    }
};


int main() {
    using namespace bsrvcore;
    auto server = std::make_unique<HttpServer>(4);

    // --- Route and Aspect Configuration ---
    server
        // 1. Global Aspect (Functional Style): A logger for all requests
        ->AddGlobalAspect(
            // Pre-service lambda: Logs when a request starts
            [](std::shared_ptr<HttpServerTask> task) {
                task->Log(LogLevel::kInfo, "Global Aspect (PRE): Request received for " +
                                               task->GetCurrentLocation());
            },
            // Post-service lambda: Logs when a request finishes
            [](std::shared_ptr<HttpServerTask> task) {
                task->Log(LogLevel::kInfo, "Global Aspect (POST): Request finished for " +
                                               task->GetCurrentLocation());
            })

        // 2. Public Route (Functional Style): No specific aspects
        ->AddRouteEntry(
            HttpRequestMethod::kGet, "/public-info",
            [](std::shared_ptr<HttpServerTask> task) {
                task->SetBody("This is public information.");
            })

        // 3. Secure Route (OOP Style): Protected by the AuthAspect
        ->AddRouteEntry(HttpRequestMethod::kGet, "/secure/data",
                        std::make_unique<SecretDataHandler>())
        
        // 4. Apply the AuthAspect specifically to the secure route
        ->AddAspect(HttpRequestMethod::kGet, "/secure/data",
                    std::make_unique<AuthAspect>())

        ->AddListen({{}, 8082}); // Listen on port 8082

    // --- Start the server ---
    if (server->Start(4)) {
        std::cout << "AOP example server started on port 8082." << std::endl;
        std::cout << "Try accessing /public-info and /secure/data" << std::endl;
        std::cout << "Press Enter to stop the server..." << std::endl;
        getchar();
        server->Stop();
    } else {
        std::cerr << "Failed to start server." << std::endl;
    }

    return 0;
}
```

As you can see, in `AOP` programming, you can also choose the functional style or the OOP style to define your aspects.

## Build and Install

Currently, **bsrvcore** can only be built and installed using `CMake`. If you would like to build and install this library, please make sure you have installed the following dependencies:

- A C++20 compiler (e.g., GCC 10+, Clang 10+, MSVC 2019+)
- A very new version of CMake (e.g., 3.25+)
- Boost libraries (e.g., 1.81+), including:
  - Boost.Asio
  - Boost.Beast
  - Boost.JSON
  - Boost.URL
  - Boost.Log
  - Boost.System
  - Boost.Thread

Then, you can follow these steps to build and install **bsrvcore**:

```bash
git clone http://github.com/haomingbai/bsrvcore.git
cd bsrvcore
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)  # or use `cmake --build . -- -
sudo make install
```

If you would like to build with documentation, you also need to install `Doxygen`, then run:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target doc
sudo make install
```

The project does not depend directly on `OpenSSL`, but if you want to enable `HTTPS` support, you need to install `OpenSSL` and make sure CMake can find it.

Besides, though **bsrvcore** does not depend on any Log library, it is recommended to use `spdlog` or other logging libraries to provide a logger support since the default logger will just ignore all log messages because it should avoid all kinds of extra output overhead.

If you are going to process with RESTful API, it is also recommended to install a JSON library to parse and and serialize JSON data. For example, `Boost.JSON` or `nlohmann/json` are both good choices.

---

## TODO

- [ ] Add examples.
- [ ] Enable CTest for unit tests.
- [ ] Repair the conflicts between `HttpServer::AddGlobalAspect` and `HttpServerTask::DoCycle`.
- [ ] Improve the documentations with more details and examples.
- [ ] Add CPake support to make a release.
- [ ] Add CI/CD support.
- [ ] Release the 0.1.0 version.
