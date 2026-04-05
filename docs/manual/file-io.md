# File I/O

This chapter maps to:

- `include/bsrvcore/file/file_reader.h`
- `include/bsrvcore/file/file_writer.h`
- `include/bsrvcore/file/file_state.h`

## One-sentence idea

Use `FileWriter` for bytes already in memory and `FileReader` for files already
on disk, then chain async read/write operations through shared state objects.

## Main types

- `FileWriter`: owns a memory buffer and can write it to disk asynchronously.
- `FileReader`: holds a filesystem path and can read that file asynchronously.
- `FileWritingState`: result of one async write.
- `FileReadingState`: result of one async read.

Both reader and writer hold:

- a work executor for the blocking disk step
- a callback executor for the completion callback

You can pass one executor for both roles or two executors explicitly.
Both types are shared-only and are created through `Create(...)`.

## FileWriter basics

```cpp
bsrvcore::IoContext ioc;

auto writer = bsrvcore::FileWriter::Create(
  "hello",
  ioc.get_executor());

writer->AsyncWriteToDisk(
  "/tmp/demo.txt",
  [](std::shared_ptr<bsrvcore::FileWritingState> state) {
    if (state->ec) {
      return;
    }
    // state->reader now points at /tmp/demo.txt
  });
```

Useful methods:

- `IsValid()`
- `Size()`
- `Data()`
- `CopyTo(...)`
- `AsyncWriteToDisk(...)`

## FileReader basics

```cpp
bsrvcore::IoContext ioc;

auto reader = bsrvcore::FileReader::Create(
  "/tmp/demo.txt",
  ioc.get_executor());

reader->AsyncReadFromDisk(
  [](std::shared_ptr<bsrvcore::FileReadingState> state) {
    if (state->ec) {
      return;
    }
    // state->writer now owns the file bytes in memory
  });
```

Useful methods:

- `IsValid()`
- `GetPath()`
- `AsyncReadFromDisk(...)`

## State objects

`FileWritingState` contains:

- `path`
- `ec`
- `size`
- `reader`

`FileReadingState` contains:

- `path`
- `ec`
- `size`
- `writer`

The immediate `bool` return from `AsyncReadFromDisk(...)` /
`AsyncWriteToDisk(...)` only means the operation was scheduled successfully.
The final `state->ec` tells you whether the actual disk I/O worked.

## Chaining

Because each state can hand you the opposite file object, chaining is direct:

```cpp
reader->AsyncReadFromDisk(
  [](std::shared_ptr<bsrvcore::FileReadingState> read_state) {
    if (read_state->ec || !read_state->writer) {
      return;
    }

    read_state->writer->AsyncWriteToDisk("/tmp/copy.txt");
  });
```

Next: [Request body processing](request-body-processing.md)
