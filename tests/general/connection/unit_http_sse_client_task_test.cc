#include <gtest/gtest.h>

#include <future>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bsrvcore/http_request_method.h"
#include "bsrvcore/http_server.h"
#include "bsrvcore/http_server_task.h"
#include "bsrvcore/http_sse_client_task.h"
#include "bsrvcore/sse_event_parser.h"
#include "test_http_client_task.h"

namespace {
namespace http = boost::beast::http;
using bsrvcore::test::ServerGuard;
using bsrvcore::test::StartServerWithRoutes;

}  // namespace

TEST(HttpSseClientTaskTest, StartAndNextPullEvents) {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/events",
      [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
        task->SetField(http::field::content_type, "text/event-stream; charset=utf-8");
        task->SetBody("data: one\\n\\ndata: two\\n\\n");
      });

  ServerGuard guard(std::move(server));
  const auto port = StartServerWithRoutes(guard);

  boost::asio::io_context ioc;
  auto client = bsrvcore::HttpSseClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), "/events");

  auto parser = bsrvcore::AllocateShared<bsrvcore::SseEventParser>();
  auto events = bsrvcore::AllocateShared<std::vector<bsrvcore::SseEvent>>();
  auto completion = bsrvcore::AllocateShared<std::promise<void>>();
  auto future = completion->get_future();
  auto done = bsrvcore::AllocateShared<bool>(false);
  auto saw_next = bsrvcore::AllocateShared<bool>(false);

  std::function<void()> pull_next;
  pull_next = [client, parser, events, completion, done, saw_next, &pull_next]() {
    client->Next([parser, events, completion, done, saw_next, &pull_next](
                     const bsrvcore::HttpSseClientResult& result) {
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

  client->Start([completion, done, &pull_next](const bsrvcore::HttpSseClientResult& result) {
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
