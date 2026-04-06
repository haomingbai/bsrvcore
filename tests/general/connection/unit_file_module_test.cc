#include <gtest/gtest.h>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/file/file_reader.h"
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
  IoContextRunner work_runner;
  IoContextRunner callback_runner;

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
  IoContextRunner work_runner;
  IoContextRunner callback_runner;

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
  IoContextRunner work_runner;
  IoContextRunner callback_runner;

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
  IoContextRunner work_runner;
  IoContextRunner callback_runner;

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
