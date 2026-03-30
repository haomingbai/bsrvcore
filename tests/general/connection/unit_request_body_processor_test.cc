#include <gtest/gtest.h>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>

#include "bsrvcore/connection/server/multipart_parser.h"
#include "bsrvcore/connection/server/put_processor.h"

namespace {
namespace http = boost::beast::http;

class IoContextRunner {
 public:
  IoContextRunner()
      : guard_(boost::asio::make_work_guard(ioc_)),
        thread_([this]() { ioc_.run(); }) {}

  ~IoContextRunner() {
    guard_.reset();
    ioc_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  boost::asio::io_context& Get() noexcept { return ioc_; }

 private:
  boost::asio::io_context ioc_;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      guard_;
  std::thread thread_;
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

bool WaitForFileContents(const std::filesystem::path& path,
                         const std::string& expected) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec &&
        ReadFile(path) == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

std::string BuildMultipartBody() {
  return "--boundary123\r\n"
         "Content-Disposition: form-data; name=\"meta\"\r\n"
         "Content-Type: text/plain\r\n"
         "\r\n"
         "hello\r\n"
         "--boundary123\r\n"
         "Content-Disposition: form-data; name=\"upload\"; "
         "filename=\"note.txt\"\r\n"
         "Content-Type: application/octet-stream\r\n"
         "\r\n"
         "file-body\r\n"
         "--boundary123--\r\n";
}

}  // namespace

TEST(RequestBodyProcessorTest, MultipartParserExposesPartUtilities) {
  bsrvcore::HttpRequest request;
  request.set(http::field::content_type,
              "multipart/form-data; boundary=\"boundary123\"");
  request.body() = BuildMultipartBody();

  bsrvcore::MultipartParser parser(request);

  EXPECT_EQ(parser.GetPartCount(), 2u);
  EXPECT_EQ(parser.GetPartType(0), "text/plain");
  EXPECT_EQ(parser.GetPartType(1), "application/octet-stream");
  EXPECT_FALSE(parser.IsFile(0));
  EXPECT_TRUE(parser.IsFile(1));
}

TEST(RequestBodyProcessorTest, MultipartParserAsyncDumpWritesFilePart) {
  IoContextRunner runner;

  bsrvcore::HttpRequest request;
  request.set(http::field::content_type,
              "multipart/form-data; boundary=boundary123");
  request.body() = BuildMultipartBody();

  bsrvcore::MultipartParser parser(request, runner.Get().get_executor());
  const auto path = MakeTempPath("multipart");
  std::promise<bool> promise;
  auto future = promise.get_future();

  ASSERT_TRUE(parser.AsyncDumpToDisk(
      1, path, [&promise](bool ok) { promise.set_value(ok); }));
  EXPECT_TRUE(future.get());
  EXPECT_EQ(ReadFile(path), "file-body");

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(RequestBodyProcessorTest, MultipartParserRejectsNonFilePartDump) {
  IoContextRunner runner;

  bsrvcore::HttpRequest request;
  request.set(http::field::content_type,
              "multipart/form-data; boundary=boundary123");
  request.body() = BuildMultipartBody();

  bsrvcore::MultipartParser parser(request, runner.Get().get_executor());
  EXPECT_FALSE(
      parser.AsyncDumpToDisk(0, MakeTempPath("multipart-noop"), [](bool) {}));
}

TEST(RequestBodyProcessorTest, MultipartParserRequiresExecutorForAsyncDump) {
  bsrvcore::HttpRequest request;
  request.set(http::field::content_type,
              "multipart/form-data; boundary=boundary123");
  request.body() = BuildMultipartBody();

  bsrvcore::MultipartParser parser(request);
  EXPECT_FALSE(parser.AsyncDumpToDisk(1, MakeTempPath("multipart-no-exec")));
}

TEST(RequestBodyProcessorTest, PutProcessorAsyncDumpWritesPutBody) {
  IoContextRunner runner;

  bsrvcore::HttpRequest request{http::verb::put, "/upload", 11};
  request.body() = "put-payload";

  bsrvcore::PutProcessor processor(request, runner.Get().get_executor());
  const auto path = MakeTempPath("put");
  std::promise<bool> promise;
  auto future = promise.get_future();

  ASSERT_TRUE(processor.AsyncDumpToDisk(
      path, [&promise](bool ok) { promise.set_value(ok); }));
  EXPECT_TRUE(future.get());
  EXPECT_EQ(ReadFile(path), "put-payload");

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(RequestBodyProcessorTest, PutProcessorRejectsNonPutRequests) {
  IoContextRunner runner;

  bsrvcore::HttpRequest request{http::verb::post, "/upload", 11};
  request.body() = "post-body";

  bsrvcore::PutProcessor processor(request, runner.Get().get_executor());
  EXPECT_FALSE(
      processor.AsyncDumpToDisk(MakeTempPath("put-noop"), [](bool) {}));
}

TEST(RequestBodyProcessorTest, PutProcessorRequiresExecutorForAsyncDump) {
  bsrvcore::HttpRequest request{http::verb::put, "/upload", 11};
  request.body() = "ignored-callback";

  bsrvcore::PutProcessor processor(request);
  EXPECT_FALSE(processor.AsyncDumpToDisk(MakeTempPath("put-no-exec")));
}

TEST(RequestBodyProcessorTest,
     PutProcessorCanIgnoreCompletionCallbackWhenExecutorProvided) {
  IoContextRunner runner;

  bsrvcore::HttpRequest request{http::verb::put, "/upload", 11};
  request.body() = "ignored-callback";

  bsrvcore::PutProcessor processor(request, runner.Get().get_executor());
  const auto path = MakeTempPath("put-ignore");

  ASSERT_TRUE(processor.AsyncDumpToDisk(path));
  EXPECT_TRUE(WaitForFileContents(path, "ignored-callback"));

  std::error_code ec;
  std::filesystem::remove(path, ec);
}
