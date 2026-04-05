/**
 * @file example_sse_stream.cc
 * @brief Server-side SSE stream with counter events and heartbeats.
 *
 * Demonstrates:
 * - Manual connection management for SSE
 * - WriteHeader / WriteBody for streaming
 * - Two independent timers in one handler
 * - Counter events and heartbeat comments
 *
 * Prerequisites: Boost, OpenSSL (required by bsrvcore build).
 * Build: cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=ON
 * Run: ./build/examples/example_sse_stream
 */

// BEGIN README_SNIPPET: sse_stream

#include <atomic>
#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"

namespace {

constexpr std::size_t kCounterIntervalMs = 1000;
constexpr std::size_t kHeartbeatIntervalMs = 2500;
constexpr std::size_t kCloseDelayMs = 200;
constexpr std::uint64_t kMaxCounterEvents = 5;

struct SseStreamState {
  std::atomic<bool> stopped{false};
  std::uint64_t next_value{1};
};

bool ShouldStop(const std::shared_ptr<bsrvcore::HttpServerTask>& task,
                const std::shared_ptr<SseStreamState>& state) {
  if (state->stopped.load()) {
    return true;
  }

  if (!task->IsAvailable()) {
    state->stopped.store(true);
    return true;
  }

  return false;
}

void ScheduleCounter(const std::shared_ptr<bsrvcore::HttpServerTask>& task,
                     const std::shared_ptr<SseStreamState>& state);

void ScheduleHeartbeat(const std::shared_ptr<bsrvcore::HttpServerTask>& task,
                       const std::shared_ptr<SseStreamState>& state);

void StopStream(const std::shared_ptr<bsrvcore::HttpServerTask>& task,
                const std::shared_ptr<SseStreamState>& state) {
  bool expected = false;
  if (!state->stopped.compare_exchange_strong(expected, true)) {
    return;
  }

  if (!task->IsAvailable()) {
    return;
  }

  task->WriteBody(
      "event: done\n"
      "data: counter limit reached\n\n");
  task->SetTimer(kCloseDelayMs, [task] {
    if (task->IsAvailable()) {
      task->DoClose();
    }
  });
}

void ScheduleCounter(const std::shared_ptr<bsrvcore::HttpServerTask>& task,
                     const std::shared_ptr<SseStreamState>& state) {
  task->SetTimer(kCounterIntervalMs, [task, state] {
    if (ShouldStop(task, state)) {
      return;
    }

    const std::uint64_t value = state->next_value++;
    task->WriteBody("id: " + std::to_string(value) +
                    "\n"
                    "event: counter\n"
                    "data: " +
                    std::to_string(value) + "\n\n");

    if (value >= kMaxCounterEvents) {
      StopStream(task, state);
      return;
    }

    ScheduleCounter(task, state);
  });
}

void ScheduleHeartbeat(const std::shared_ptr<bsrvcore::HttpServerTask>& task,
                       const std::shared_ptr<SseStreamState>& state) {
  task->SetTimer(kHeartbeatIntervalMs, [task, state] {
    if (ShouldStop(task, state)) {
      return;
    }

    task->WriteBody(": heartbeat\n\n");
    ScheduleHeartbeat(task, state);
  });
}

}  // namespace

int main() {
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(2);
  server
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/events",
          [](const std::shared_ptr<bsrvcore::HttpServerTask>& task) {
            task->SetManualConnectionManagement(true);

            bsrvcore::HttpResponseHeader header;
            header.version(task->GetRequest().version());
            header.result(bsrvcore::HttpStatus::ok);
            header.set(bsrvcore::HttpField::content_type,
                       "text/event-stream; charset=utf-8");
            header.set(bsrvcore::HttpField::cache_control, "no-cache");
            header.set(bsrvcore::HttpField::connection, "keep-alive");

            task->WriteHeader(std::move(header));
            task->WriteBody(": stream opened\n\n");

            auto state = bsrvcore::AllocateShared<SseStreamState>();
            ScheduleCounter(task, state);
            ScheduleHeartbeat(task, state);
          })
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8086}, 1);

  if (!server->Start()) {
    std::cerr << "Failed to start server." << '\n';
    return 1;
  }

  std::cout << "Listening on http://0.0.0.0:8086/events" << '\n';
  std::cout << "Try: curl -N http://127.0.0.1:8086/events" << '\n';
  std::cout << "The stream closes after " << kMaxCounterEvents
            << " counter events." << '\n';
  std::cout << "Press Enter to stop." << '\n';
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: sse_stream
