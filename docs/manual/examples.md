# Examples

This chapter maps to the `examples/` folder.

## Build examples

Examples are enabled by default in this repository.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBSRVCORE_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

## Run examples

```bash
./build/examples/example_quick_start
./build/examples/example_configuration
./build/examples/example_oop_handler
./build/examples/example_aspect_basic
./build/examples/example_logger_custom
./build/examples/example_session_context
```

See also the top-level [README.md](../../README.md) for example source links.
