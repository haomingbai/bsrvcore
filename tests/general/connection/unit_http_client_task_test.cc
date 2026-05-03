#include <gtest/gtest.h>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json/array.hpp>
#include <boost/json/error.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_ref.hpp>
#include <boost/json/value_to.hpp>
#include <boost/system/errc.hpp>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "bsrvcore/connection/client/client_stream.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/stream_builder.h"
#include "bsrvcore/connection/client/stream_slot.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/types.h"
#include "bsrvcore/route/http_request_method.h"
#include "test_http_client_task.h"

namespace {
namespace http = boost::beast::http;
namespace json = bsrvcore::json;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;

}  // namespace

TEST(ClientStreamTest, TracksEmptyTcpSslAndMoveStates) {
  bsrvcore::IoContext ioc;
  bsrvcore::ClientStream stream;

  EXPECT_TRUE(stream.Empty());
  EXPECT_FALSE(stream.HasStream());

  stream.EmplaceTcp(ioc.get_executor());
  EXPECT_FALSE(stream.Empty());
  EXPECT_TRUE(stream.HasStream());
  EXPECT_TRUE(stream.IsTcp());
  EXPECT_FALSE(stream.IsSsl());
  static_cast<void>(stream.Tcp());

  stream.Reset();
  EXPECT_TRUE(stream.Empty());

  bsrvcore::SslContext ssl_ctx(bsrvcore::SslContext::tls_client);
  stream.EmplaceSsl(ioc.get_executor(), ssl_ctx);
  EXPECT_TRUE(stream.IsSsl());
  EXPECT_FALSE(stream.IsTcp());
  static_cast<void>(stream.Ssl());

  bsrvcore::ClientStream moved = std::move(stream);
  EXPECT_TRUE(moved.IsSsl());
  moved.Reset();
  EXPECT_TRUE(moved.Empty());
}

TEST(HttpClientTaskTest, BasicGetDoneCallbackSuccess) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/ping",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody("pong");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  bsrvcore::IoContext ioc;
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

TEST(HttpClientTaskTest, RawFactoryUsesProvidedConnectedTcpStream) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/raw",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody("raw-ok");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  bsrvcore::IoContext ioc;
  bsrvcore::TcpStream stream(ioc.get_executor());
  boost::system::error_code connect_ec;
  stream.socket().connect(
      bsrvcore::TcpEndpoint(boost::asio::ip::make_address("127.0.0.1"), port),
      connect_ec);
  ASSERT_FALSE(connect_ec);

  auto task = bsrvcore::HttpClientTask::CreateHttpRaw(
      ioc.get_executor(), std::move(stream), "127.0.0.1", "/raw",
      http::verb::get);

  std::promise<bsrvcore::HttpClientResult> promise;
  auto future = promise.get_future();
  task->OnDone([&promise](const bsrvcore::HttpClientResult& result) {
    promise.set_value(result);
  });

  task->Start();
  ioc.run();

  const auto result = future.get();
  EXPECT_FALSE(result.ec);
  EXPECT_EQ(result.response.result(), http::status::ok);
  EXPECT_EQ(result.response.body(), "raw-ok");
}

TEST(HttpClientTaskTest,
     HttpsRawFactoryValidatesProvidedSslStreamInsteadOfSslCtx) {
  bsrvcore::IoContext ioc;
  bsrvcore::SslContext ssl_ctx(bsrvcore::SslContext::tls_client);
  bsrvcore::SslStream stream(ioc.get_executor(), ssl_ctx);

  auto task = bsrvcore::HttpClientTask::CreateHttpsRaw(
      ioc.get_executor(), std::move(stream), "127.0.0.1", "/raw-https",
      http::verb::get);

  std::promise<bsrvcore::HttpClientResult> promise;
  auto future = promise.get_future();
  task->OnDone([&promise](const bsrvcore::HttpClientResult& result) {
    promise.set_value(result);
  });

  task->Start();
  ioc.run();

  const auto result = future.get();
  EXPECT_TRUE(result.ec);
  EXPECT_NE(result.error_stage, bsrvcore::HttpClientErrorStage::kCreate);
}

TEST(HttpClientTaskTest, HttpsFactoryWithoutSslContextFailsInBuilderPhase) {
  bsrvcore::IoContext ioc;
  auto task = bsrvcore::HttpClientTask::CreateHttps(
      ioc.get_executor(), bsrvcore::SslContextPtr{}, "127.0.0.1", "443", "/",
      http::verb::get);

  std::promise<bsrvcore::HttpClientResult> promise;
  auto future = promise.get_future();
  task->OnDone([&promise](const bsrvcore::HttpClientResult& result) {
    promise.set_value(result);
  });

  task->Start();
  ioc.run();

  const auto result = future.get();
  EXPECT_EQ(result.ec, make_error_code(boost::system::errc::invalid_argument));
  EXPECT_EQ(result.error_stage, bsrvcore::HttpClientErrorStage::kConnect);
}

TEST(HttpClientTaskTest, ExplicitPipelineHandsBuilderStreamToRawTask) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/assembled",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody("assembled-ok");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  bsrvcore::IoContext ioc;
  bsrvcore::HttpClientOptions options;

  auto assembler = std::make_shared<bsrvcore::DefaultRequestAssembler>();
  auto builder = bsrvcore::DirectStreamBuilder::Create();

  bsrvcore::HttpClientRequest request;
  request.method(http::verb::get);
  request.target("/assembled");
  request.version(11);

  auto assembled =
      assembler->Assemble(std::move(request), options, "http", "127.0.0.1",
                          std::to_string(port), nullptr);

  std::shared_ptr<bsrvcore::HttpClientTask> task;
  std::promise<bsrvcore::HttpClientResult> promise;
  auto future = promise.get_future();

  builder->Acquire(
      assembled.connection_key, ioc.get_executor(),
      [&](boost::system::error_code ec, bsrvcore::StreamSlot slot) mutable {
        if (ec) {
          bsrvcore::HttpClientResult result;
          result.ec = ec;
          promise.set_value(std::move(result));
          return;
        }

        if (!slot.HasTcpStream()) {
          bsrvcore::HttpClientResult result;
          result.ec = make_error_code(boost::system::errc::not_connected);
          promise.set_value(std::move(result));
          return;
        }

        task = bsrvcore::HttpClientTask::CreateHttpRaw(
            ioc.get_executor(), std::move(slot.TcpStreamRef()),
            assembled.connection_key.host,
            std::string(assembled.request.target()), assembled.request.method(),
            options);
        task->Request() = std::move(assembled.request);
        task->OnDone([&promise](const bsrvcore::HttpClientResult& result) {
          promise.set_value(result);
        });
        task->Start();
      });

  ioc.run();

  const auto result = future.get();
  EXPECT_FALSE(result.ec);
  EXPECT_EQ(result.response.result(), http::status::ok);
  EXPECT_EQ(result.response.body(), "assembled-ok");
}

TEST(HttpClientTaskTest, ChunkCallbackReceivesBodyData) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/chunk",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody("chunked-body-data");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  bsrvcore::IoContext ioc;
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

TEST(HttpClientTaskTest, HttpsUrlWithoutSslContextStillCreatesTask) {
  bsrvcore::IoContext ioc;
  auto task = bsrvcore::HttpClientTask::CreateFromUrl(
      ioc.get_executor(), "https://127.0.0.1:8443/ping", http::verb::get);

  ASSERT_NE(task, nullptr);
  EXPECT_EQ(task->Request().method(), http::verb::get);
}

TEST(HttpClientTaskTest, JsonHelpersRoundTripRequestAndResponse) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
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

  bsrvcore::IoContext ioc;
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
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
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
    bsrvcore::IoContext ioc;
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
  bsrvcore::IoContext ioc;
  auto task = bsrvcore::HttpClientTask::CreateFromUrl(
      ioc.get_executor(), "http://127.0.0.1:8080/ping", http::verb::get);

  auto chained = task->OnConnected([](const bsrvcore::HttpClientResult&) {})
                     ->OnHeader([](const bsrvcore::HttpClientResult&) {})
                     ->OnChunk([](const bsrvcore::HttpClientResult&) {})
                     ->OnDone([](const bsrvcore::HttpClientResult&) {});

  EXPECT_EQ(chained.get(), task.get());
}

TEST(HttpClientTaskTest, DoneCallbackUsesConfiguredCallbackExecutor) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/callback",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetBody("ok");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  bsrvcore::IoContext io_ioc;
  bsrvcore::IoContext callback_ioc;
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
