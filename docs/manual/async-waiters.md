# Async Waiters

This chapter maps to:

- `include/bsrvcore/core/async_waiter.h`

## One-sentence idea

Use async waiters when several independent callbacks must finish before one
final callback can run.

## Available helpers

- `AsyncTemplateWaiter<Ts...>`
- `AsyncSameTypeWaiter<T>`
- `AsyncSameTypeWaiter<void>`

All of them are:

- header-only
- single-use
- callback-convergence helpers only
- shared-only objects created through `Create(...)`

They do **not** hop between executors by themselves. The final callback runs on
the thread that completes the waiter.

## AsyncTemplateWaiter

Use this when each branch returns a different type.

```cpp
auto waiter =
  bsrvcore::AsyncTemplateWaiter<int, std::string>::Create();

waiter->OnReady([](int code, std::string body) {
  // both branches finished
});

auto callbacks = waiter->MakeCallbacks();
std::get<0>(callbacks)(200);
std::get<1>(callbacks)("ok");
```

You can also register the tuple form directly:

```cpp
waiter->OnTupleReady([](std::tuple<int, std::string> values) {
  // same completion point, tuple form
});
```

## AsyncSameTypeWaiter

Use this when all branches return the same type and the count is only known at
runtime.

```cpp
auto waiter = bsrvcore::AsyncSameTypeWaiter<std::string>::Create(3);

waiter->OnReady([](std::vector<std::string> values) {
  // values[0], values[1], values[2]
});

auto callbacks = waiter->MakeCallbacks();
callbacks[0]("a");
callbacks[1]("b");
callbacks[2]("c");
```

## Void specialization

Use `AsyncSameTypeWaiter<void>` when you only care that N tasks finished:

```cpp
auto waiter = bsrvcore::AsyncSameTypeWaiter<void>::Create(2);
waiter->OnReady([]() {
  // both branches finished
});

auto callbacks = waiter->MakeCallbacks();
callbacks[0]();
callbacks[1]();
```

## Practical rule

Waiters are a good fit for:

- reading multiple files before building one request
- collecting several async metadata probes
- converging a small fixed or runtime-sized fan-out

Next: [Client tasks (HTTP/HTTPS + SSE)](client-tasks.md)
