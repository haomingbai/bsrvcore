#include <gtest/gtest.h>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "bsrvcore/core/types.h"
#include "bsrvcore/file/file_reader.h"
#include "bsrvcore/file/file_state.h"
#include "bsrvcore/file/file_writer.h"

namespace {

class IoContextRunner {
 public:
  IoContextRunner()
      : guard_(boost::asio::make_work_guard(ioc_)),
        ready_future_(ready_.get_future()),
        thread_([this]() {
          ready_.set_value(std::this_thread::get_id());
          ioc_.run();
        }) {
    // std::thread starts during member initialization. Passing the id through
    // the promise avoids racing a worker write against later member init.
    thread_id_ = ready_future_.get();
  }

  ~IoContextRunner() {
    guard_.reset();
    ioc_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  bsrvcore::IoContext& Get() noexcept { return ioc_; }
  std::thread::id GetThreadId() const noexcept { return thread_id_; }

 private:
  bsrvcore::IoContext ioc_;
  bsrvcore::IoWorkGuard guard_;
  std::promise<std::thread::id> ready_;
  std::future<std::thread::id> ready_future_;
  std::thread thread_;
  std::thread::id thread_id_{};
};

std::filesystem::path MakeTempPath(const std::string& prefix) {
  static std::size_t counter = 0;
  const auto id = counter++;
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (prefix + "-" + std::to_string(now) + "-" + std::to_string(id));
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void WriteFile(const std::filesystem::path& path, const std::string& body) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << body;
}

}  // namespace

TEST(FileModuleTest, FileWriterAsyncWriteProducesReaderState) {
  // Declared so that the work runner is destroyed first: its io thread is
  // joined before the callback io_context is torn down on the main thread.
  IoContextRunner callback_runner;
  IoContextRunner work_runner;

  auto writer = bsrvcore::FileWriter::Create(
      "file-writer-body", work_runner.Get().get_executor(),
      callback_runner.Get().get_executor());
  const auto path = MakeTempPath("file-writer");

  std::promise<
      std::pair<std::shared_ptr<bsrvcore::FileWritingState>, std::thread::id>>
      promise;
  auto future = promise.get_future();

  ASSERT_TRUE(writer->AsyncWriteToDisk(
      path, [&promise](std::shared_ptr<bsrvcore::FileWritingState> state) {
        promise.set_value({std::move(state), std::this_thread::get_id()});
      }));

  auto [state, callback_thread] = future.get();
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->ec);
  EXPECT_EQ(state->path, path);
  EXPECT_EQ(state->size, std::string("file-writer-body").size());
  ASSERT_TRUE(state->reader);
  EXPECT_EQ(state->reader->GetPath(), path);
  EXPECT_EQ(callback_thread, callback_runner.GetThreadId());
  EXPECT_EQ(ReadFile(path), "file-writer-body");

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(FileModuleTest, FileReaderAsyncReadProducesWriterState) {
  // Declared so that the work runner is destroyed first: its io thread is
  // joined before the callback io_context is torn down on the main thread.
  IoContextRunner callback_runner;
  IoContextRunner work_runner;

  const auto path = MakeTempPath("file-reader");
  WriteFile(path, "reader-body");

  auto reader =
      bsrvcore::FileReader::Create(path, work_runner.Get().get_executor(),
                                   callback_runner.Get().get_executor());

  std::promise<
      std::pair<std::shared_ptr<bsrvcore::FileReadingState>, std::thread::id>>
      promise;
  auto future = promise.get_future();

  ASSERT_TRUE(reader->AsyncReadFromDisk(
      [&promise](std::shared_ptr<bsrvcore::FileReadingState> state) {
        promise.set_value({std::move(state), std::this_thread::get_id()});
      }));

  auto [state, callback_thread] = future.get();
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->ec);
  EXPECT_EQ(state->path, path);
  EXPECT_EQ(state->size, std::string("reader-body").size());
  ASSERT_TRUE(state->writer);
  EXPECT_TRUE(state->writer->IsValid());
  EXPECT_EQ(state->writer->Size(), std::string("reader-body").size());
  EXPECT_EQ(std::string(state->writer->Data(),
                        state->writer->Data() + state->writer->Size()),
            "reader-body");
  EXPECT_EQ(callback_thread, callback_runner.GetThreadId());

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(FileModuleTest, FileWriterCopyToValidatesBufferSize) {
  auto writer = bsrvcore::FileWriter::Create("copy-me");
  char ok_buffer[7] = {};
  char small_buffer[3] = {};

  EXPECT_TRUE(writer->CopyTo(ok_buffer, sizeof(ok_buffer)));
  EXPECT_EQ(std::string(ok_buffer, ok_buffer + writer->Size()), "copy-me");
  EXPECT_FALSE(writer->CopyTo(small_buffer, sizeof(small_buffer)));
}

TEST(FileModuleTest, FileWriterOutlivesCallerDuringAsyncWrite) {
  // Declared so that the work runner is destroyed first: its io thread is
  // joined before the callback io_context is torn down on the main thread.
  IoContextRunner callback_runner;
  IoContextRunner work_runner;

  const auto path = MakeTempPath("file-writer-lifetime");
  auto writer = bsrvcore::FileWriter::Create(
      "lifetime-body", work_runner.Get().get_executor(),
      callback_runner.Get().get_executor());

  std::promise<std::shared_ptr<bsrvcore::FileWritingState>> promise;
  auto future = promise.get_future();
  ASSERT_TRUE(writer->AsyncWriteToDisk(
      path,
      [&promise](std::shared_ptr<bsrvcore::FileWritingState> state) mutable {
        promise.set_value(std::move(state));
      }));
  writer.reset();

  auto state = future.get();
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->ec);
  EXPECT_EQ(ReadFile(path), "lifetime-body");

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(FileModuleTest, FileReaderOutlivesCallerDuringAsyncRead) {
  // Declared so that the work runner is destroyed first: its io thread is
  // joined before the callback io_context is torn down on the main thread.
  IoContextRunner callback_runner;
  IoContextRunner work_runner;

  const auto path = MakeTempPath("file-reader-lifetime");
  WriteFile(path, "reader-lifetime-body");

  auto reader =
      bsrvcore::FileReader::Create(path, work_runner.Get().get_executor(),
                                   callback_runner.Get().get_executor());

  std::promise<std::shared_ptr<bsrvcore::FileReadingState>> promise;
  auto future = promise.get_future();
  ASSERT_TRUE(reader->AsyncReadFromDisk(
      [&promise](std::shared_ptr<bsrvcore::FileReadingState> state) mutable {
        promise.set_value(std::move(state));
      }));
  reader.reset();

  auto state = future.get();
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->ec);
  ASSERT_TRUE(state->writer);
  EXPECT_EQ(std::string(state->writer->Data(),
                        state->writer->Data() + state->writer->Size()),
            "reader-lifetime-body");

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

namespace {

// Regression test for the data race fixed in FileWriter::AsyncWriteToDisk():
// FileWritingState::path is now published synchronously on the calling thread
// before the write operation is posted, so concurrent main-thread reads of
// state->path are ordered against it. The write operation itself must never
// touch the shared state's path again.
TEST(FileModuleTest, ConcurrentAsyncWritesAllowConcurrentStatePathReads) {
  constexpr std::size_t kCallerThreads = 4;
  constexpr std::size_t kDistinctOps = 8;
  constexpr std::size_t kSharedPathOps = 2;

  bsrvcore::IoContext work_ioc;
  auto work_guard = boost::asio::make_work_guard(work_ioc);
  std::vector<std::thread> io_threads;
  io_threads.reserve(kCallerThreads);
  for (std::size_t i = 0; i < kCallerThreads; ++i) {
    io_threads.emplace_back([&work_ioc]() { work_ioc.run(); });
  }
  IoContextRunner callback_runner;

  auto writer = bsrvcore::FileWriter::Create(
      "concurrent-writer-body", work_ioc.get_executor(),
      callback_runner.Get().get_executor());
  ASSERT_TRUE(writer->IsValid());

  struct WriteEntry {
    std::filesystem::path path;
    std::shared_ptr<bsrvcore::FileWritingState> state;
    std::shared_ptr<std::atomic<bool>> started;
    std::shared_ptr<std::atomic<bool>> failed;
    std::promise<std::shared_ptr<bsrvcore::FileWritingState>> promise;
    std::future<std::shared_ptr<bsrvcore::FileWritingState>> future;
  };

  const std::string payload = "concurrent-writer-body";

  std::vector<WriteEntry> entries;
  entries.reserve(kDistinctOps + kSharedPathOps);
  for (std::size_t i = 0; i < kDistinctOps; ++i) {
    auto& entry = entries.emplace_back();
    entry.path = MakeTempPath("file-writer-concurrent-" + std::to_string(i));
    entry.state = bsrvcore::FileWritingState::Create();
    entry.started = std::make_shared<std::atomic<bool>>(false);
    entry.failed = std::make_shared<std::atomic<bool>>(false);
    entry.future = entry.promise.get_future();
  }
  // Two callers write the same destination path with identical bytes, so the
  // final file content stays deterministic no matter which write lands last.
  const auto shared_path = MakeTempPath("file-writer-concurrent-shared");
  for (std::size_t i = 0; i < kSharedPathOps; ++i) {
    auto& entry = entries.emplace_back();
    entry.path = shared_path;
    entry.state = bsrvcore::FileWritingState::Create();
    entry.started = std::make_shared<std::atomic<bool>>(false);
    entry.failed = std::make_shared<std::atomic<bool>>(false);
    entry.future = entry.promise.get_future();
  }

  std::vector<std::thread> callers;
  callers.reserve(entries.size());
  for (auto& entry : entries) {
    callers.emplace_back([&writer, &entry]() {
      const bool ok = writer->AsyncWriteToDisk(
          entry.path, entry.state,
          [&entry](std::shared_ptr<bsrvcore::FileWritingState> state) {
            entry.promise.set_value(std::move(state));
          });
      if (!ok) {
        entry.failed->store(true, std::memory_order_relaxed);
      }
      // Published after AsyncWriteToDisk() returned, which is exactly when
      // state->path has been written on this (calling) thread.
      entry.started->store(true, std::memory_order_release);
    });
  }

  // Main thread reads state->path (and queries writer state) concurrently with
  // the in-flight writes. This is the window that raced when the path was
  // assigned inside the posted operation.
  for (int round = 0; round < 4; ++round) {
    for (auto& entry : entries) {
      while (!entry.started->load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      ASSERT_FALSE(entry.failed->load(std::memory_order_acquire));
      EXPECT_EQ(entry.state->path, entry.path);
      EXPECT_TRUE(writer->IsValid());
      EXPECT_NE(writer->Size(), std::size_t{0});
    }
  }

  for (auto& caller : callers) {
    caller.join();
  }

  for (auto& entry : entries) {
    ASSERT_FALSE(entry.failed->load(std::memory_order_acquire));
    auto state = entry.future.get();
    ASSERT_TRUE(state);
    EXPECT_FALSE(state->ec);
    EXPECT_EQ(state->path, entry.path);
    EXPECT_EQ(state->size, payload.size());
    ASSERT_TRUE(state->reader);
  }
  // All writes are complete, so no further open(trunc) can interfere with the
  // content reads below.
  EXPECT_EQ(ReadFile(shared_path), payload);
  for (auto& entry : entries) {
    if (entry.path != shared_path) {
      EXPECT_EQ(ReadFile(entry.path), payload);
    }
  }

  work_guard.reset();
  work_ioc.stop();
  for (auto& io_thread : io_threads) {
    io_thread.join();
  }

  std::error_code ec;
  std::filesystem::remove(shared_path, ec);
  for (auto& entry : entries) {
    std::filesystem::remove(entry.path, ec);
  }
}

}  // namespace
