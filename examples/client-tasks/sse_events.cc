/**
 * @file example_sse_events.cc
 * @brief Consume an SSE endpoint with HttpSseClientTask and SseEventParser.
 *
 * Demonstrates:
 * - starting an SSE client task
 * - repeatedly pulling chunks with Next()
 * - feeding incremental response data into SseEventParser
 */

#include <bsrvcore/allocator/allocator.h>
#include <bsrvcore/connection/client/http_sse_client_task.h>
#include <bsrvcore/connection/client/sse_event_parser.h>

#include <boost/asio/io_context.hpp>
#include <boost/system/system_error.hpp>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main() {
  bsrvcore::IoContext ioc;

  auto client = bsrvcore::HttpSseClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", "8080", "/events");

  auto parser = bsrvcore::AllocateShared<bsrvcore::SseEventParser>();
  auto events = bsrvcore::AllocateShared<std::vector<bsrvcore::SseEvent>>();
  auto done = bsrvcore::AllocateShared<bool>(false);
  auto completion = bsrvcore::AllocateShared<std::promise<void>>();
  auto future = completion->get_future();

  std::function<void()> pull_next;
  pull_next = [client, parser, events, done, completion, &pull_next]() {
    // Next() returns the next incremental body chunk, not a fully parsed SSE
    // event. The example keeps a tiny pull loop so parser state stays explicit.
    client->Next([parser, events, done, completion,
                  &pull_next](const bsrvcore::HttpSseClientResult& result) {
      if (*done) {
        return;
      }

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

  client->Start([done, completion,
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

  try {
    future.get();
    std::cout << "Received events: " << events->size() << '\n';
  } catch (const std::exception& ex) {
    std::cerr << "SSE stream failed: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
