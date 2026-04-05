#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <boost/beast/http.hpp>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/multipart_generator.h"
#include "bsrvcore/connection/client/put_generator.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"
#include "test_http_client_task.h"

namespace {

namespace http = boost::beast::http;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;

std::filesystem::path MakeTempPath(const std::string& prefix) {
  static std::size_t counter = 0;
  return std::filesystem::temp_directory_path() /
         (prefix + "-" + std::to_string(counter++));
}

void WriteFile(const std::filesystem::path& path, const std::string& body) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << body;
}

bsrvcore::HttpClientResult RunPreparedTask(
    std::shared_ptr<bsrvcore::HttpClientTask> task, bsrvcore::IoContext& ioc) {
  std::promise<bsrvcore::HttpClientResult> promise;
  auto future = promise.get_future();
  task->OnDone([&promise](const bsrvcore::HttpClientResult& result) {
    promise.set_value(result);
  });
  task->Start();
  ioc.run();
  ioc.restart();
  return future.get();
}

}  // namespace

TEST(UploadClientTest, PutGeneratorReadsFileAndCreatesRunnableTask) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kPut, "/echo",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody(task->GetRequest().body());
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto path = MakeTempPath("put-client");
  WriteFile(path, "put-client-body");

  bsrvcore::IoContext ioc;
  auto reader = bsrvcore::FileReader::Create(path, ioc.get_executor(),
                                             ioc.get_executor());
  auto client = bsrvcore::PutGenerator::CreateHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), "/echo");
  client->SetFile(reader).SetContentType("application/octet-stream");

  std::promise<std::shared_ptr<bsrvcore::HttpClientTask>> ready_promise;
  auto ready_future = ready_promise.get_future();
  ASSERT_TRUE(client->AsyncCreateTask(
      [&ready_promise](std::error_code ec,
                       std::shared_ptr<bsrvcore::HttpClientTask> task) mutable {
        EXPECT_FALSE(ec);
        ready_promise.set_value(std::move(task));
      }));

  ioc.run();
  ioc.restart();
  auto task = ready_future.get();
  ASSERT_TRUE(task);

  const auto result = RunPreparedTask(std::move(task), ioc);
  EXPECT_FALSE(result.ec);
  EXPECT_EQ(result.response.result(), http::status::ok);
  EXPECT_EQ(result.response.body(), "put-client-body");

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(UploadClientTest,
     MultipartGeneratorBuildsMultipartBodyWithTextAndFileParts) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/echo-multipart",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetField(
            http::field::content_type,
            std::string(task->GetRequest()[http::field::content_type]));
        task->SetBody(task->GetRequest().body());
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto path = MakeTempPath("multipart-client");
  WriteFile(path, "multipart-file-body");

  bsrvcore::IoContext ioc;
  auto reader = bsrvcore::FileReader::Create(path, ioc.get_executor(),
                                             ioc.get_executor());
  auto client = bsrvcore::MultipartGenerator::CreateHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), "/echo-multipart");
  client->AddTextPart("meta", "hello")
      .AddFilePart("upload", reader, "note.txt", "application/octet-stream");

  std::promise<std::shared_ptr<bsrvcore::HttpClientTask>> ready_promise;
  auto ready_future = ready_promise.get_future();
  ASSERT_TRUE(client->AsyncCreateTask(
      [&ready_promise](std::error_code ec,
                       std::shared_ptr<bsrvcore::HttpClientTask> task) mutable {
        EXPECT_FALSE(ec);
        ready_promise.set_value(std::move(task));
      }));

  ioc.run();
  ioc.restart();
  auto task = ready_future.get();
  ASSERT_TRUE(task);

  const auto result = RunPreparedTask(std::move(task), ioc);
  EXPECT_FALSE(result.ec);
  EXPECT_EQ(result.response.result(), http::status::ok);
  EXPECT_NE(std::string(result.response[http::field::content_type])
                .find("multipart/form-data; boundary="),
            std::string::npos);
  EXPECT_NE(result.response.body().find("name=\"meta\""), std::string::npos);
  EXPECT_NE(result.response.body().find("hello"), std::string::npos);
  EXPECT_NE(result.response.body().find("name=\"upload\""), std::string::npos);
  EXPECT_NE(result.response.body().find("filename=\"note.txt\""),
            std::string::npos);
  EXPECT_NE(result.response.body().find("multipart-file-body"),
            std::string::npos);

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(UploadClientTest, PutGeneratorReportsErrorForMissingReader) {
  bsrvcore::IoContext ioc;
  auto client = bsrvcore::PutGenerator::CreateHttp(
      ioc.get_executor(), "127.0.0.1", "80", "/missing");

  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  ASSERT_TRUE(client->AsyncCreateTask(
      [&promise](std::error_code ec,
                 std::shared_ptr<bsrvcore::HttpClientTask> task) mutable {
        EXPECT_FALSE(task);
        promise.set_value(ec);
      }));

  ioc.run();
  EXPECT_TRUE(static_cast<bool>(future.get()));
}

TEST(UploadClientTest, PutGeneratorOutlivesCallerDuringTaskPreparation) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kPut, "/echo",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody(task->GetRequest().body());
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto path = MakeTempPath("put-generator-lifetime");
  WriteFile(path, "put-generator-body");

  bsrvcore::IoContext ioc;
  auto reader = bsrvcore::FileReader::Create(path, ioc.get_executor(),
                                             ioc.get_executor());
  auto client = bsrvcore::PutGenerator::CreateHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), "/echo");
  client->SetFile(reader);

  std::promise<std::shared_ptr<bsrvcore::HttpClientTask>> ready_promise;
  auto ready_future = ready_promise.get_future();
  ASSERT_TRUE(client->AsyncCreateTask(
      [&ready_promise](std::error_code ec,
                       std::shared_ptr<bsrvcore::HttpClientTask> task) mutable {
        EXPECT_FALSE(ec);
        ready_promise.set_value(std::move(task));
      }));
  client.reset();

  ioc.run();
  ioc.restart();
  auto task = ready_future.get();
  ASSERT_TRUE(task);
  EXPECT_EQ(RunPreparedTask(std::move(task), ioc).response.body(),
            "put-generator-body");

  std::error_code ec;
  std::filesystem::remove(path, ec);
}
