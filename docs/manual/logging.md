# Logging

This chapter maps to `include/bsrvcore/core/logger.h` and logging helpers on tasks.

## Provide a logger

By default, you may not see logs unless a logger is installed.

```cpp
auto logger = std::make_shared<MyLogger>();
server->SetLogger(logger);
```

## Log from a handler

```cpp
task->Log(bsrvcore::LogLevel::kInfo, "Handling request");
```

The server also exposes `HttpServer::Log(level, message)`.

## Implement a logger

Implement `bsrvcore::Logger` and override its `Log()` method. Keep it fast and thread-safe.

## Use a logger in bsrvrun

`bsrvrun` can install a logger from YAML through a runtime plugin:

```yaml
logger:
  factory: "/opt/bsrv/plugins/libfile_logger.so"
  params:
    path: "/var/log/bsrvrun.log"
    prefix: "runtime|"
```

The plugin should export `GetLoggerFactory()` and implement
`bsrvcore::bsrvrun::LoggerFactory::Create(ParameterMap*)`.

See also: [bsrvrun runtime container](bsrvrun.md).

Next: [Client tasks](client-tasks.md).
