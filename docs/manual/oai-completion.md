# OAI completion (chat)

This chapter maps to:

- `include/bsrvcore/oai/completion/oai_completion.h`
- `examples/oai/completion.cc`

## One-sentence idea

`OaiCompletionFactory` is a small facade that calls **DeepSeek/OpenAI compatible**
`/chat/completions` APIs and returns an immutable `OaiCompletionState` chain.

## Data model

- `OaiCompletionInfo`: provider connection info (`base_url`, `api_key`, ...)
- `OaiModelInfo`: model name plus model-specific JSON parameters
- `OaiMessage`: one chat message (`role`, `message`, `tool_calls`, `reasoning`)
- `OaiCompletionState`: one immutable node (message + request log + previous state)
- `OaiRequestLog`: HTTP status, error message, request id, finish reason, timestamp, ...

## Base URL

`base_url` is joined as:

```text
<base_url>/chat/completions
```

So if your provider expects a `/v1` prefix, pass `base_url` with that suffix:

- `https://api.openai.com/v1`
- `https://api.deepseek.com` (provider-dependent)

## Build an immutable message chain

Each `AppendMessage()` returns a new state node pointing to the previous node.
You can keep older states.

```cpp
bsrvcore::oai::completion::OaiCompletionFactory factory(ioc.get_executor(), info);
auto model_info = bsrvcore::AllocateShared<bsrvcore::oai::completion::OaiModelInfo>();
model_info->model = "deepseek-chat";
model_info->params["temperature"] = 0.2;

bsrvcore::oai::completion::OaiCompletionFactory::StatePtr state = nullptr;
state = factory.AppendMessage({"system", "You are a helpful assistant.", {}}, state);
state = factory.AppendMessage({"user", "Say hello.", {}}, state);
```

## Non-stream completion

Use `FetchCompletion()`.

```cpp
factory.FetchCompletion(state, model_info, [](auto done_state) {
  const auto& log = done_state->GetLog();
  const auto& msg = done_state->GetMessage();
  // log.status / log.http_status_code / log.error_message ...
  // msg.message / msg.reasoning / msg.tool_calls
});
```

## Stream completion (SSE)

Use `FetchStreamCompletion()`.

- `on_delta(delta)` is called for each non-empty `content` delta.
- `on_reasoning_delta(delta)` is called for each non-empty `reasoning_content` delta.
- `on_done(state)` is called exactly once when the stream finishes (or fails).

```cpp
factory.FetchStreamCompletion(
  state,
  model_info,
  [](auto done_state) {
    // Final aggregated message is in done_state->GetMessage().message
    // Final aggregated reasoning is in done_state->GetMessage().reasoning
  },
  [](const std::string& delta) {
    // Print incremental content
  },
  [](const std::string& reasoning_delta) {
    // Print incremental reasoning_content (optional)
  }
);
```

## Tools and tool_calls

There are overloads that accept a `std::vector<OaiToolDefinition>`.
`OaiToolDefinition::parameters` is now a `bsrvcore::JsonObject`.

When the provider responds with tool calls, the final assistant message contains
`OaiToolCall` entries with the accumulated JSON `arguments`.

## Reasoning (reasoning_content)

Some providers (notably reasoning-focused models such as `deepseek-reasoner`)
return an extra field `reasoning_content` alongside `content`.

- Non-stream: parsed from `choices[0].message.reasoning_content`
- Stream: accumulated from `choices[0].delta.reasoning_content` into the final
  `done_state->GetMessage().reasoning`

The 3-argument stream overload (`on_done` + `on_delta`) remains **content-only**.
If you want reasoning deltas, use the 4-argument overload with
`on_reasoning_delta`.

## Example program

Build examples:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBSRVCORE_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

Run:

```bash
OAI_API_KEY=... ./build/examples/oai/example_oai_completion \
  --base_url https://api.deepseek.com \
  --model deepseek-chat \
  --user "Hello"

OAI_API_KEY=... ./build/examples/oai/example_oai_completion \
  --base_url https://api.deepseek.com \
  --model deepseek-reasoner \
  --user "Solve: 25*17" \
  --print_reasoning

OAI_API_KEY=... ./build/examples/oai/example_oai_completion \
  --base_url https://api.deepseek.com \
  --model deepseek-chat \
  --user "Hello" \
  --stream
```

Note: passing API keys on the command line can be visible to other users via
process listings. Prefer `OAI_API_KEY=...` when possible.

Next: [Examples](examples.md).
