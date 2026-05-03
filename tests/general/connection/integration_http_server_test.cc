/**
 * @file integration_http_server_test.cc
 * @brief End-to-end server tests for core request lifecycle behavior.
 *
 * Focus:
 * - basic request/response flow
 * - connection-cap enforcement
 * - aspect ordering
 * - async handler lifetime semantics
 * - request-body helpers such as PutProcessor
 */

#include <gtest/gtest.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json/error.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <boost/system/errc.hpp>
#include <chrono>
#include <compare>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/connection/server/put_processor.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/core/types.h"
#include "bsrvcore/route/http_request_method.h"
#include "test_http_client_task.h"

namespace {
using bsrvcore::test::DoRequestWithRetry;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;
namespace http = boost::beast::http;
namespace json = bsrvcore::json;

std::filesystem::path MakeTempPath(const std::string& prefix) {
  static std::size_t counter = 0;
  return std::filesystem::temp_directory_path() /
         (prefix + "-" + std::to_string(counter++));
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string ThreadIdToString(std::thread::id id) {
  std::ostringstream oss;
  oss << id;
  return oss.str();
}

bool WaitUntilSocketClosed(boost::asio::ip::tcp::socket& socket,
                           std::chrono::milliseconds timeout) {
  boost::system::error_code set_non_blocking_ec;
  socket.non_blocking(true, set_non_blocking_ec);
  if (set_non_blocking_ec) {
    return false;
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  char byte = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    boost::system::error_code read_ec;
    const std::size_t bytes =
        socket.read_some(boost::asio::buffer(&byte, 1), read_ec);
    if (!read_ec && bytes > 0) {
      return false;
    }
    if (read_ec == boost::asio::error::would_block ||
        read_ec == boost::asio::error::try_again) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    if (read_ec == boost::asio::error::eof ||
        read_ec == boost::asio::error::connection_reset ||
        read_ec == boost::asio::error::operation_aborted ||
        read_ec == boost::asio::error::bad_descriptor) {
      return true;
    }
    if (read_ec) {
      return true;
    }
  }

  return false;
}

}  // namespace

// Verify basic GET/POST handling end-to-end.
TEST(HttpServerIntegrationTest, BasicGetAndPost) {
  auto server = std::make_unique<bsrvcore::HttpServer>(4);
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

TEST(HttpServerIntegrationTest, MaxConnectionDropsExcessSockets) {
  bsrvcore::HttpServerRuntimeOptions options;
  options.core_thread_num = 2;
  options.max_thread_num = 2;
  options.has_max_connection = true;
  options.max_connection = 1;

  auto server = std::make_unique<bsrvcore::HttpServer>(options);
  server->SetHeaderReadExpiry(500)->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/ping",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetBody("pong");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  bsrvcore::IoContext ioc;
  boost::asio::ip::tcp::socket first(ioc);
  first.connect({boost::asio::ip::make_address("127.0.0.1"), port});

  boost::asio::ip::tcp::socket second(ioc);
  second.connect({boost::asio::ip::make_address("127.0.0.1"), port});

  EXPECT_TRUE(WaitUntilSocketClosed(second, std::chrono::milliseconds(1500)));

  boost::system::error_code ignore_ec;
  second.close(ignore_ec);
  first.close(ignore_ec);

  bool recovered = false;
  for (int i = 0; i < 100; ++i) {
    try {
      const auto res = DoRequestWithRetry(http::verb::get, port, "/ping", "");
      if (res.result() == http::status::ok && res.body() == "pong") {
        recovered = true;
        break;
      }
    } catch (const std::exception&) {
      // Release path is async; retry in a short loop.
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  EXPECT_TRUE(recovered);
}

// Verify aspect order across global/method/subtree/terminal hooks.
TEST(HttpServerIntegrationTest, AspectOrderIsDeterministic) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);

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
      ->AddAspect(
          bsrvcore::HttpRequestMethod::kGet, "/order",
          [](std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
            task->AppendBody("preP|");
          },
          [](std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
            task->AppendBody("postP|");
          })
      ->AddAspect(
          bsrvcore::HttpRequestMethod::kGet, "/order/leaf",
          [](std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
            task->AppendBody("preS|");
          },
          [](std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
            task->AppendBody("postS|");
          })
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/order/leaf",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->AppendBody("handler|");
                      })
      ->AddTerminalAspect(
          bsrvcore::HttpRequestMethod::kGet, "/order/leaf",
          [](std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
            task->AppendBody("preT|");
          },
          [](std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
            task->AppendBody("postT|");
          });

  ServerGuard guard(std::move(server));
  auto port = StartServerWithRoutes(guard);

  auto res = DoRequestWithRetry(http::verb::get, port, "/order/leaf", "");
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(),
            "preG|preM|preP|preS|preT|handler|postT|postS|postP|postM|postG|");
}

// Verify post phase starts only after service task references are released.
TEST(HttpServerIntegrationTest, PostPhaseWaitsForServiceTaskRelease) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  auto held_task =
      std::make_shared<std::shared_ptr<bsrvcore::HttpServerTask>>();

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
  auto server = std::make_unique<bsrvcore::HttpServer>(2);

  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPut, "/dump",
      [path](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        auto processor = bsrvcore::PutProcessor::Create(*task);
        const bool scheduled = processor->AsyncDumpToDisk(
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

TEST(HttpServerIntegrationTest, JsonHelpersParseAndSetObjectBody) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/json",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        bsrvcore::JsonObject request_json;
        const bsrvcore::JsonErrorCode ec = task->ParseRequestJson(request_json);
        if (ec) {
          task->GetResponse().result(http::status::bad_request);
          task->SetBody(ec.message());
          return;
        }

        bsrvcore::JsonObject response_json;
        response_json["echo"] = request_json;
        task->SetJson(std::move(response_json));
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto res = DoRequestWithRetry(
      http::verb::post, port, "/json", R"({"name":"bsrvcore","count":2})",
      [](http::request<http::string_body>& request) {
        request.set(http::field::content_type, "application/json");
      });

  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res[http::field::content_type], "application/json");

  bsrvcore::JsonErrorCode ec;
  const bsrvcore::JsonValue root = json::parse(res.body(), ec);
  ASSERT_FALSE(ec);
  ASSERT_TRUE(root.is_object());
  const auto& body = root.as_object();
  const auto& echoed = body.at("echo").as_object();
  EXPECT_EQ(json::value_to<std::string>(echoed.at("name")), "bsrvcore");
  EXPECT_EQ(echoed.at("count").as_int64(), 2);
}

TEST(HttpServerIntegrationTest, ParseRequestJsonRejectsNonObjectRoot) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/json-object-only",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        bsrvcore::JsonObject request_json;
        const bsrvcore::JsonErrorCode ec = task->ParseRequestJson(request_json);
        task->GetResponse().result(ec ? http::status::bad_request
                                      : http::status::ok);
        if (ec == json::error::not_object) {
          task->SetBody("not-object");
          return;
        }
        task->SetBody(ec ? "bad-json" : "ok");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto res =
      DoRequestWithRetry(http::verb::post, port, "/json-object-only", "[]");
  EXPECT_EQ(res.result(), http::status::bad_request);
  EXPECT_EQ(res.body(), "not-object");
}

TEST(HttpServerIntegrationTest, TryParseRequestJsonReturnsFalseOnSyntaxError) {
  auto server = std::make_unique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kPost, "/json-try-parse",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        bsrvcore::JsonValue request_json;
        task->SetBody(task->TryParseRequestJson(request_json) ? "ok" : "bad");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  const auto res =
      DoRequestWithRetry(http::verb::post, port, "/json-try-parse", R"({"x":)");
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), "bad");
}

TEST(HttpServerIntegrationTest,
     RouteThreadingKeepsNormalLifecycleOnIoAndComputingHandlerOnWorker) {
  bsrvcore::HttpServerRuntimeOptions options;
  options.core_thread_num = 1;
  options.max_thread_num = 1;

  auto server = std::make_unique<bsrvcore::HttpServer>(options);
  server
      ->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/io",
                      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                        task->AppendBody(
                            "handler:" +
                            ThreadIdToString(std::this_thread::get_id()) + "|");
                      })
      ->AddTerminalAspect(
          bsrvcore::HttpRequestMethod::kGet, "/io",
          [](std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
            task->AppendBody(
                "pre:" + ThreadIdToString(std::this_thread::get_id()) + "|");
          },
          [](std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
            task->AppendBody(
                "post:" + ThreadIdToString(std::this_thread::get_id()) + "|");
          })
      ->AddComputingRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/cpu",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->AppendBody(
                "handler:" + ThreadIdToString(std::this_thread::get_id()) +
                "|");
          })
      ->AddTerminalAspect(
          bsrvcore::HttpRequestMethod::kGet, "/cpu",
          [](std::shared_ptr<bsrvcore::HttpPreServerTask> task) {
            task->AppendBody(
                "pre:" + ThreadIdToString(std::this_thread::get_id()) + "|");
          },
          [](std::shared_ptr<bsrvcore::HttpPostServerTask> task) {
            task->AppendBody(
                "post:" + ThreadIdToString(std::this_thread::get_id()) + "|");
          });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  auto io_promise = std::make_shared<std::promise<std::thread::id>>();
  auto worker_promise = std::make_shared<std::promise<std::thread::id>>();
  auto io_future = io_promise->get_future();
  auto worker_future = worker_promise->get_future();

  guard.server->PostToIoContext(
      [io_promise] { io_promise->set_value(std::this_thread::get_id()); });
  guard.server->Post([worker_promise] {
    worker_promise->set_value(std::this_thread::get_id());
  });

  ASSERT_EQ(io_future.wait_for(std::chrono::seconds(10)),
            std::future_status::ready);
  ASSERT_EQ(worker_future.wait_for(std::chrono::seconds(10)),
            std::future_status::ready);

  const auto io_thread_id = io_future.get();
  const auto worker_thread_id = worker_future.get();

  const auto io_res = DoRequestWithRetry(http::verb::get, port, "/io", "");
  const auto cpu_res = DoRequestWithRetry(http::verb::get, port, "/cpu", "");
  const auto io_thread = ThreadIdToString(io_thread_id);
  const auto worker_thread = ThreadIdToString(worker_thread_id);

  EXPECT_EQ(io_res.result(), http::status::ok);
  EXPECT_EQ(cpu_res.result(), http::status::ok);
  EXPECT_EQ(io_res.body(), "pre:" + io_thread + "|handler:" + io_thread +
                               "|post:" + io_thread + "|");
  EXPECT_EQ(cpu_res.body(), "pre:" + io_thread + "|handler:" + worker_thread +
                                "|post:" + io_thread + "|");
  EXPECT_NE(io_res.body(), cpu_res.body());
}
