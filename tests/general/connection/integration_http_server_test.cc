#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "bsrvcore/put_processor.h"
#include "test_http_client_task.h"

namespace {
using bsrvcore::test::DoRequestWithRetry;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;
namespace http = boost::beast::http;

std::filesystem::path MakeTempPath(const std::string& prefix) {
  static std::size_t counter = 0;
  return std::filesystem::temp_directory_path() /
         (prefix + "-" + std::to_string(counter++));
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

// Verify basic GET/POST handling end-to-end.
TEST(HttpServerIntegrationTest, BasicGetAndPost) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(4);
  server
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/ping",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetBody("pong");
                      })
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kPost, "/echo",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->SetBody(task->GetRequest().body());
                      });

  ServerGuard guard(std::move(server));
  auto port = StartServerWithRoutes(guard);

  auto get_res = DoRequestWithRetry(http::verb::get, port, "/ping", "");
  EXPECT_EQ(get_res.result(), http::status::ok);
  EXPECT_EQ(get_res.body(), "pong");

  auto post_res = DoRequestWithRetry(http::verb::post, port, "/echo", "hello");
  EXPECT_EQ(post_res.result(), http::status::ok);
  EXPECT_EQ(post_res.body(), "hello");
}

// Verify aspect order across global/method/route hooks.
TEST(HttpServerIntegrationTest, AspectOrderIsDeterministic) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);

  server
      ->AddGlobalAspect(
          [](std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
            task->AppendBody("preG|");
          },
          [](std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
            task->AppendBody("postG|");
          })
      ->AddGlobalAspect(
          bsrvcore::HttpRequestMethod::kGet,
          [](std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
            task->AppendBody("preM|");
          },
          [](std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
            task->AppendBody("postM|");
          })
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/order",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->AppendBody("handler|");
                      })
      ->AddAspect(
          bsrvcore::HttpRequestMethod::kGet, "/order",
          [](std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
            task->AppendBody("preR|");
          },
          [](std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
            task->AppendBody("postR|");
          });

  ServerGuard guard(std::move(server));
  auto port = StartServerWithRoutes(guard);

  auto res = DoRequestWithRetry(http::verb::get, port, "/order", "");
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "preG|preM|preR|handler|postR|postM|postG|");
}

// Verify post phase starts only after service task references are released.
TEST(HttpServerIntegrationTest, PostPhaseWaitsForServiceTaskRelease) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  auto held_task =
      bsrvcore::AllocateShared<std::shared_ptr<bsrvcore::HttpServerTask>>();

  server
      ->AddGlobalAspect(
          [](std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
            task->AppendBody("pre|");
          },
          [](std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
            task->AppendBody("post|");
          })
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/defer",
          [held_task](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->AppendBody("handler|");
            *held_task = task;
            task->SetTimer(10, [held_task] { held_task->reset(); });
          });

  ServerGuard guard(std::move(server));
  auto port = StartServerWithRoutes(guard);

  auto res = DoRequestWithRetry(http::verb::get, port, "/defer", "");
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "pre|handler|post|");
}

TEST(HttpServerIntegrationTest, PutProcessorAsyncDumpCompletesBeforeResponse) {
  const auto path = MakeTempPath("put-dump");
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);

  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPut, "/dump",
      [path](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        bsrvcore::PutProcessor processor(*task);
        const bool scheduled = processor.AsyncDumpToDisk(
            path, [task](bool ok) { task->SetBody(ok ? "dumped" : "failed"); });
        if (!scheduled) {
          task->SetBody("rejected");
        }
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto res = DoRequestWithRetry(http::verb::put, port, "/dump", "abc123");
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "dumped");
  EXPECT_EQ(ReadFile(path), "abc123");

  std::error_code ec;
  std::filesystem::remove(path, ec);
}
