#include <bsrvcore/allocator.h>
#include <bsrvcore/http_sse_client_task.h>
#include <bsrvcore/sse_event_parser.h>

#include <boost/asio/io_context.hpp>

#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main() {
  boost::asio::io_context ioc;

  auto client = bsrvcore::HttpSseClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", "8080", "/events");

  auto parser = bsrvcore::AllocateShared<bsrvcore::SseEventParser>();
  auto events = bsrvcore::AllocateShared<std::vector<bsrvcore::SseEvent>>();
  auto done = bsrvcore::AllocateShared<bool>(false);
  auto completion = bsrvcore::AllocateShared<std::promise<void>>();
  auto future = completion->get_future();

  std::function<void()> pull_next;
  pull_next = [client, parser, events, done, completion, &pull_next]() {
    client->Next([parser, events, done, completion, &pull_next](
                     const bsrvcore::HttpSseClientResult& result) {
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

  client->Start([done, completion, &pull_next](
                    const bsrvcore::HttpSseClientResult& result) {
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
