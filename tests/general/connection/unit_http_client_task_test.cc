#include <gtest/gtest.h>

#include <boost/asio/executor_work_guard.hpp>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/json.h"
#include "bsrvcore/route/http_request_method.h"
#include "test_http_client_task.h"

namespace {
namespace http = boost::beast::http;
namespace json = bsrvcore::json;
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

TEST(HttpClientTaskTest, JsonHelpersRoundTripRequestAndResponse) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/echo-json",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetField(
            http::field::content_type,
            std::string(task->GetRequest()[http::field::content_type]));
        task->SetBody(task->GetRequest().body());
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  auto task = bsrvcore::HttpClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), "/echo-json",
      http::verb::post);

  task->SetJson(json::object{{"name", "bsrvcore"}, {"count", 2}});

  std::promise<bsrvcore::HttpClientResult> promise;
  auto future = promise.get_future();
  task->OnDone([&promise](const bsrvcore::HttpClientResult& result) {
    promise.set_value(result);
  });

  task->Start();
  ioc.run();

  const auto result = future.get();
  ASSERT_FALSE(result.ec);
  ASSERT_FALSE(result.cancelled);
  EXPECT_EQ(result.response.result(), http::status::ok);
  EXPECT_EQ(result.response[http::field::content_type], "application/json");

  bsrvcore::JsonObject response_json;
  const bsrvcore::JsonErrorCode ec = result.ParseJsonBody(response_json);
  ASSERT_FALSE(ec);
  EXPECT_EQ(json::value_to<std::string>(response_json.at("name")), "bsrvcore");
  EXPECT_EQ(response_json.at("count").as_int64(), 2);
}

TEST(HttpClientTaskTest, JsonResponseHelpersReportTypeAndSyntaxErrors) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/json-array",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetJson(json::array{"one", "two"});
                      })
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/json-invalid",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetField(http::field::content_type,
                                       "application/json");
                        task->SetBody("{bad-json");
                      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto fetch_result = [port](const std::string& target) {
    boost::asio::io_context ioc;
    auto task = bsrvcore::HttpClientTask::CreateHttp(
        ioc.get_executor(), "127.0.0.1", std::to_string(port), target,
        http::verb::get);

    std::promise<bsrvcore::HttpClientResult> promise;
    auto future = promise.get_future();
    task->OnDone([&promise](const bsrvcore::HttpClientResult& result) {
      promise.set_value(result);
    });

    task->Start();
    ioc.run();
    return future.get();
  };

  const auto array_result = fetch_result("/json-array");
  ASSERT_FALSE(array_result.ec);

  bsrvcore::JsonObject object_body;
  const bsrvcore::JsonErrorCode object_ec =
      array_result.ParseJsonBody(object_body);
  EXPECT_EQ(object_ec, json::error::not_object);

  const auto invalid_result = fetch_result("/json-invalid");
  ASSERT_FALSE(invalid_result.ec);

  bsrvcore::JsonValue invalid_body;
  EXPECT_FALSE(invalid_result.TryParseJsonBody(invalid_body));
  const bsrvcore::JsonErrorCode invalid_ec =
      invalid_result.ParseJsonBody(invalid_body);
  EXPECT_EQ(invalid_ec, json::error::syntax);
}

TEST(HttpClientTaskTest, CallbackSettersReturnSharedPointerForChaining) {
  boost::asio::io_context ioc;
  auto task = bsrvcore::HttpClientTask::CreateFromUrl(
      ioc.get_executor(), "http://127.0.0.1:8080/ping", http::verb::get);

  auto chained = task->OnConnected([](const bsrvcore::HttpClientResult&) {})
                     ->OnHeader([](const bsrvcore::HttpClientResult&) {})
                     ->OnChunk([](const bsrvcore::HttpClientResult&) {})
                     ->OnDone([](const bsrvcore::HttpClientResult&) {});

  EXPECT_EQ(chained.get(), task.get());
}

TEST(HttpClientTaskTest, DoneCallbackUsesConfiguredCallbackExecutor) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/callback",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody("ok");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context io_ioc;
  boost::asio::io_context callback_ioc;
  auto callback_guard = boost::asio::make_work_guard(callback_ioc);
  auto task = bsrvcore::HttpClientTask::CreateHttp(
      io_ioc.get_executor(), callback_ioc.get_executor(), "127.0.0.1",
      std::to_string(port), "/callback", http::verb::get);

  std::promise<std::thread::id> callback_thread_promise;
  auto callback_thread_future = callback_thread_promise.get_future();
  std::promise<std::thread::id> callback_executor_thread_promise;
  auto callback_executor_thread_future =
      callback_executor_thread_promise.get_future();

  task->OnDone([&](const bsrvcore::HttpClientResult& result) {
    EXPECT_FALSE(result.ec);
    callback_thread_promise.set_value(std::this_thread::get_id());
    callback_guard.reset();
    io_ioc.stop();
    callback_ioc.stop();
  });

  task->Start();

  std::thread io_thread([&]() { io_ioc.run(); });
  std::thread callback_thread([&]() {
    callback_executor_thread_promise.set_value(std::this_thread::get_id());
    callback_ioc.run();
  });

  const auto callback_thread_id = callback_thread_future.get();
  const auto callback_executor_thread_id =
      callback_executor_thread_future.get();

  io_thread.join();
  callback_thread.join();

  EXPECT_EQ(callback_thread_id, callback_executor_thread_id);
}
