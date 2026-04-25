#include <gtest/gtest.h>

#include <boost/asio/executor_work_guard.hpp>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bsrvcore/connection/client/http_sse_client_task.h"
#include "bsrvcore/connection/client/sse_event_parser.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"
#include "test_http_client_task.h"

namespace {
namespace http = boost::beast::http;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;

}  // namespace

TEST(HttpSseClientTaskTest, StartAndNextPullEvents) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/events",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetField(http::field::content_type,
                                         "text/event-stream; charset=utf-8");
                          task->SetBody("data: one\\n\\ndata: two\\n\\n");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  bsrvcore::IoContext ioc;
  auto client = bsrvcore::HttpSseClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), "/events");

  auto parser = bsrvcore::AllocateShared<bsrvcore::SseEventParser>();
  auto events = bsrvcore::AllocateShared<std::vector<bsrvcore::SseEvent>>();
  auto completion = bsrvcore::AllocateShared<std::promise<void>>();
  auto future = completion->get_future();
  auto done = bsrvcore::AllocateShared<bool>(false);
  auto saw_next = bsrvcore::AllocateShared<bool>(false);

  std::function<void()> pull_next;
  pull_next = [client, parser, events, completion, done, saw_next,
               &pull_next]() {
    client->Next([parser, events, completion, done, saw_next,
                  &pull_next](const bsrvcore::HttpSseClientResult& result) {
      if (*done) {
        return;
      }

      *saw_next = true;

      if (result.ec && !result.cancelled) {
        *done = true;
        completion->set_exception(
            std::make_exception_ptr(boost::system::system_error(result.ec)));
        return;
      }

      if (!result.chunk.empty()) {
        auto parsed = parser->Feed(result.chunk);
        events->insert(events->end(), parsed.begin(), parsed.end());
      }

      if (result.eof || result.cancelled) {
        *done = true;
        completion->set_value();
        return;
      }

      pull_next();
    });
  };

  client->Start([completion, done,
                 &pull_next](const bsrvcore::HttpSseClientResult& result) {
    if (result.ec || result.cancelled) {
      if (!*done) {
        *done = true;
        completion->set_exception(
            std::make_exception_ptr(boost::system::system_error(result.ec)));
      }
      return;
    }
    pull_next();
  });

  ioc.run();
  EXPECT_NO_THROW(future.get());
  EXPECT_TRUE(*saw_next);
}

TEST(HttpSseClientTaskTest, ParserParsesSamplePayload) {
  bsrvcore::SseEventParser parser;
  auto events = parser.Feed("data: one\n\n");
  auto events2 = parser.Feed("data: two\n\n");
  events.insert(events.end(), events2.begin(), events2.end());

  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].data, "one");
  EXPECT_EQ(events[1].data, "two");
}

TEST(HttpSseClientTaskTest, ParserAllocatedFeedParsesSamplePayload) {
  bsrvcore::SseEventParser parser;
  auto events = parser.FeedAllocated("data: one\n\n");
  auto events2 = parser.FeedAllocated("data: two\n\n");
  events.insert(events.end(), events2.begin(), events2.end());

  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].data, "one");
  EXPECT_EQ(events[1].data, "two");
}

TEST(HttpSseClientTaskTest, StartCallbackUsesConfiguredCallbackExecutor) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(bsrvcore::HttpRequestMethod::kGet, "/events-callback",
                        [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
                          task->SetField(http::field::content_type,
                                         "text/event-stream; charset=utf-8");
                          task->SetBody("data: ok\n\n");
                        });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  bsrvcore::IoContext io_ioc;
  bsrvcore::IoContext callback_ioc;
  auto callback_guard = boost::asio::make_work_guard(callback_ioc);
  auto task = bsrvcore::HttpSseClientTask::CreateHttp(
      io_ioc.get_executor(), callback_ioc.get_executor(), "127.0.0.1",
      std::to_string(port), "/events-callback");

  std::promise<std::thread::id> callback_thread_promise;
  auto callback_thread_future = callback_thread_promise.get_future();
  std::promise<std::thread::id> callback_executor_thread_promise;
  auto callback_executor_thread_future =
      callback_executor_thread_promise.get_future();

  task->Start([&](const bsrvcore::HttpSseClientResult& result) {
    EXPECT_FALSE(result.ec);
    callback_thread_promise.set_value(std::this_thread::get_id());
    callback_guard.reset();
    io_ioc.stop();
    callback_ioc.stop();
  });

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
