/**
 * @file example_http_client_request.cc
 * @brief Minimal outbound HTTP request using HttpClientTask.
 *
 * Demonstrates:
 * - creating a one-shot HTTP client task on an io_context
 * - waiting for completion through OnDone + std::promise
 * - inspecting the final response status and body size
 */

#include <bsrvcore/connection/client/http_client_task.h>

#include <boost/asio/io_context.hpp>
#include <boost/beast/http/verb.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <string>

int main() {
  namespace http = boost::beast::http;

  // The client task reuses the caller-provided io_context. Running the context
  // drives resolve/connect/write/read completion until OnDone fires.
  bsrvcore::IoContext ioc;

  bsrvcore::HttpClientOptions options;
  options.resolve_timeout = std::chrono::seconds(5);
  options.connect_timeout = std::chrono::seconds(5);
  options.write_timeout = std::chrono::seconds(5);
  options.read_header_timeout = std::chrono::seconds(5);
  options.read_body_timeout = std::chrono::seconds(5);
  options.user_agent = "bsrvcore-example-client";

  auto task = bsrvcore::HttpClientTask::CreateHttp(
      ioc.get_executor(), "example.com", "80", "/", http::verb::get, options);

  std::promise<bsrvcore::HttpClientResult> promise;
  auto future = promise.get_future();

  task->OnDone([&promise](const bsrvcore::HttpClientResult& result) {
    promise.set_value(result);
  });

  task->Start();
  ioc.run();

  auto result = future.get();
  if (result.ec) {
    std::cerr << "HTTP request failed: " << result.ec.message() << '\n';
    return 1;
  }

  std::cout << "Status: " << result.response.result_int() << '\n';
  std::cout << "Body size: " << result.response.body().size() << '\n';
  return 0;
}
