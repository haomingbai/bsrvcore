# bsrvrun (Runtime Web Container)

`bsrvrun` is a runtime executable packaged in the runtime component.

It reads a YAML file, loads service/handler/aspect factories from shared
libraries, and builds an `HttpServer` from configuration.

## Config path resolution

`bsrvrun` resolves config in this order:

1. CLI path: `-c` or `--config`
2. `./bsrvrun.yaml`
3. `/etc/bsrvrun/bsrvrun.yaml`

If none exists, startup fails.

## YAML format

```yaml
server:
  # Number of I/O threads (used by HttpServer::Start)
  thread_count: 4
  # Optional connection-cap control.
  has_max_connection: true
  max_connection: 4096
  # Optional worker executor settings (used by HttpServer constructor)
  executor:
    core_thread_num: 4
    max_thread_num: 8
    fast_queue_capacity: 256
    thread_clean_interval: 60000
    task_scan_interval: 100
    suspend_time: 1

listeners:
  - address: "0.0.0.0"
    port: 8080

services:
  - slot: 0
    factory: "/opt/bsrv/plugins/libgreeting_service.so"
    params:
      prefix: "service|"

global:
  default_handler:
    factory: "/opt/bsrv/plugins/libdefault_handler.so"
    params:
      service_slot: "0"
      message: "fallback"
  aspects:
    - factory: "/opt/bsrv/plugins/librequest_log_aspect.so"
      params:
        level: "info"

routes:
  - method: "GET"
    path: "/hello/{name}"
    ignore_default_route: false
    cpu: false
    handler:
      factory: "/opt/bsrv/plugins/libhello_handler.so"
      params:
        service_slot: "0"
        greeting: "hello"
    aspects:
      - factory: "/opt/bsrv/plugins/libauth_aspect.so"
        params:
          token: "demo"
```

## Route behavior notes

## Server threading notes

- `server.thread_count` controls I/O thread count for accept/read/write.
- `server.has_max_connection` + `server.max_connection` controls approximate
  accepted-connection cap.
- `server.executor` controls worker pool behavior for `Post`, `Dispatch`, computing routes, and timer callbacks.
- If `server.executor` is omitted, worker core/max thread count falls back to `server.thread_count` for backward compatibility.

- `method` supports only existing `HttpRequestMethod` values:
  - `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `HEAD`
- `path` follows the same parameter syntax as C++ API (for example `/users/{id}`).
- `ignore_default_route: true` maps to exclusive route registration.
- `cpu: true` maps the route to `AddComputingRouteEntry()` semantics so the handler body runs on the worker pool while keeping the normal task lifecycle.

## Service behavior notes

- `services` is an optional top-level array.
- Each service item requires:
  - `slot`: non-negative integer service slot index
  - `factory`: shared-library path
  - `params`: optional string:string map passed to `GenerateService()`
- `bsrvrun` rejects duplicate `slot` values at startup.
- Services are created during config application and destroyed after
  `HttpServer::Stop()` completes.
- Handler/aspect code can read a slot with `task->GetService<T>(slot)` when the
  plugin shares the concrete service type definition.

## Plugin ABI contract

Public ABI headers live under `include/bsrvcore/bsrvrun/`.

- `bsrvcore::bsrvrun::String`
- `bsrvcore::bsrvrun::ParameterMap`
- `bsrvcore::bsrvrun::ServiceFactory`
- `bsrvcore::bsrvrun::HttpRequestHandlerFactory`
- `bsrvcore::bsrvrun::HttpRequestAspectHandlerFactory`

Each plugin should export one fixed symbol:

- Handler plugin: `GetHandlerFactory`
- Aspect plugin: `GetAspectFactory`
- Service plugin: `GetServiceFactory`

Both exports should use the provided export macros so the symbol is visible
from shared libraries on every platform, including Windows DLLs:

- `BSRVCORE_BSRVRUN_HANDLER_FACTORY_EXPORT`
- `BSRVCORE_BSRVRUN_ASPECT_FACTORY_EXPORT`
- `BSRVCORE_BSRVRUN_SERVICE_FACTORY_EXPORT`

These macros include `extern "C"` and the required export visibility
attributes. The returned factory pointer is not owned by `HttpServer`.

Plugins that previously exported only `extern "C"` should be rebuilt with
these macros before they are loaded on Windows. Otherwise `bsrvrun` can fail
to find exported factories via `GetProcAddress()`.

### Service plugin example

```cpp
#include <bsrvcore/bsrvcore.h>

// Shared plugin header:
struct GreetingService {
  std::string prefix;
};

class GreetingFactory : public bsrvcore::bsrvrun::ServiceFactory {
 public:
  void* GenerateService(bsrvcore::bsrvrun::ParameterMap* params) override {
    auto* service = new GreetingService{"service|"};
    if (params != nullptr) {
      const auto prefix =
          params->Get(bsrvcore::bsrvrun::String("prefix")).ToStdString();
      if (!prefix.empty()) {
        service->prefix = prefix;
      }
    }
    return service;
  }

  void DestroyService(void* service) override {
    delete static_cast<GreetingService*>(service);
  }
};

BSRVCORE_BSRVRUN_SERVICE_FACTORY_EXPORT GetServiceFactory() {
  static GreetingFactory factory;
  return &factory;
}
```

### Handler plugin example

```cpp
#include <bsrvcore/bsrvcore.h>

class HelloFactory : public bsrvcore::bsrvrun::HttpRequestHandlerFactory {
 public:
  std::unique_ptr<bsrvcore::HttpRequestHandler> Get(
      bsrvcore::bsrvrun::ParameterMap* params) override {
    (void)params;
    return std::make_unique<bsrvcore::FunctionRouteHandler<std::function<void(std::shared_ptr<bsrvcore::HttpServerTask>)>>>(
        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
          std::string body = "hello from plugin";
          if (auto* service = task->GetService<GreetingService>(0)) {
            body = service->prefix + body;
          }
          task->SetBody(std::move(body));
        });
  }
};

BSRVCORE_BSRVRUN_HANDLER_FACTORY_EXPORT GetHandlerFactory() {
  static HelloFactory factory;
  return &factory;
}
```

## Run

```bash
bsrvrun -c ./bsrvrun.yaml
```
