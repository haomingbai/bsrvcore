#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/route/http_request_method.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "test_http_client_task.h"

namespace {
namespace http = boost::beast::http;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;

}  // namespace

TEST(HttpClientTaskTest, BasicGetDoneCallbackSuccess) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/ping",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody("pong");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  auto task = bsrvcore::HttpClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), "/ping",
      http::verb::get);

  std::promise<bsrvcore::HttpClientResult> promise;
  auto future = promise.get_future();

  task->OnDone([&promise](const bsrvcore::HttpClientResult& result) {
    promise.set_value(result);
  });

  task->Start();
  ioc.run();

  auto result = future.get();
  EXPECT_FALSE(result.ec);
  EXPECT_FALSE(result.cancelled);
  EXPECT_EQ(result.stage, bsrvcore::HttpClientStage::kDone);
  EXPECT_EQ(result.response.result(), http::status::ok);
  EXPECT_EQ(result.response.body(), "pong");
}

TEST(HttpClientTaskTest, ChunkCallbackReceivesBodyData) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/chunk",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody("chunked-body-data");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  auto task = bsrvcore::HttpClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), "/chunk",
      http::verb::get);

  std::string merged;
  std::promise<bsrvcore::HttpClientResult> done_promise;
  auto done_future = done_promise.get_future();

  task->OnChunk([&merged](const bsrvcore::HttpClientResult& result) {
    if (!result.ec) {
      merged += result.chunk;
    }
  });

  task->OnDone([&done_promise](const bsrvcore::HttpClientResult& result) {
    done_promise.set_value(result);
  });

  task->Start();
  ioc.run();

  auto done = done_future.get();
  EXPECT_FALSE(done.ec);
  EXPECT_EQ(merged, "chunked-body-data");
}

TEST(HttpClientTaskTest, HttpsUrlWithoutSslContextFailsAtCreateStage) {
  boost::asio::io_context ioc;
  auto task = bsrvcore::HttpClientTask::CreateFromUrl(
      ioc.get_executor(), "https://127.0.0.1:8443/ping", http::verb::get);

  std::promise<bsrvcore::HttpClientResult> promise;
  auto future = promise.get_future();

  task->OnDone([&promise](const bsrvcore::HttpClientResult& result) {
    promise.set_value(result);
  });

  task->Start();
  ioc.run();

  auto result = future.get();
  EXPECT_TRUE(result.ec);
  EXPECT_EQ(result.error_stage, bsrvcore::HttpClientErrorStage::kCreate);
  EXPECT_TRUE(task->Failed());
  EXPECT_EQ(task->ErrorStage(), bsrvcore::HttpClientErrorStage::kCreate);
}
