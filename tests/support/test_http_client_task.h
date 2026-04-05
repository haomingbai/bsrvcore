#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/core/http_server.h"

namespace bsrvcore::test {

using tcp = Tcp;
namespace http = boost::beast::http;

// Bind to port 0 to get an available ephemeral port.
inline unsigned short FindFreePort() {
  bsrvcore::IoContext ioc;
  // Bind to port 0 so the OS selects a free ephemeral port. Note there is still
  // a race window between releasing this port and binding the real server.
  tcp::acceptor acceptor(ioc, {boost::asio::ip::make_address("127.0.0.1"), 0});
  return acceptor.local_endpoint().port();
}

// RAII guard to stop the server on scope exit.
struct ServerGuard {
  explicit ServerGuard(bsrvcore::OwnedPtr<bsrvcore::HttpServer> srv)
      : server(std::move(srv)) {}

  ~ServerGuard() {
    // Stop is expected to be safe to call multiple times.
    if (server) {
      server->Stop();
    }
  }

  bsrvcore::OwnedPtr<bsrvcore::HttpServer> server;
};

template <typename ConfigureRequestFn>
inline http::response<http::string_body> DoRequestTask(
    http::verb method, unsigned short port, const std::string& target,
    const std::string& body, ConfigureRequestFn&& configure_request) {
  bsrvcore::IoContext ioc;

  HttpClientOptions options;
  options.resolve_timeout = std::chrono::seconds(2);
  options.connect_timeout = std::chrono::seconds(2);
  options.write_timeout = std::chrono::seconds(2);
  options.read_header_timeout = std::chrono::seconds(2);
  options.read_body_timeout = std::chrono::seconds(2);
  options.user_agent = "bsrvcore-test-client-task";

  auto task =
      HttpClientTask::CreateHttp(ioc.get_executor(), "127.0.0.1",
                                 std::to_string(port), target, method, options);

  // Populate request body before Start(). prepare_payload() is called
  // internally.
  task->Request().body() = body;
  configure_request(task->Request());

  // Bridge the async task API into a synchronous helper used by tests.
  std::promise<http::response<http::string_body>> promise;
  auto future = promise.get_future();

  task->OnDone([&promise](const HttpClientResult& result) mutable {
    // OnDone is the single convergence point (success/failure/cancel).
    if (result.ec) {
      promise.set_exception(
          std::make_exception_ptr(boost::system::system_error(result.ec)));
      return;
    }
    if (result.cancelled) {
      promise.set_exception(std::make_exception_ptr(
          std::runtime_error("request cancelled unexpectedly")));
      return;
    }
    promise.set_value(result.response);
  });

  task->Start();
  // Run the event loop until the request reaches OnDone.
  ioc.run();

  return future.get();
}

inline http::response<http::string_body> DoRequestTask(
    http::verb method, unsigned short port, const std::string& target,
    const std::string& body = "") {
  return DoRequestTask(method, port, target, body,
                       [](http::request<http::string_body>&) {});
}

template <typename ConfigureRequestFn>
inline http::response<http::string_body> DoRequestTaskWithRetry(
    http::verb method, unsigned short port, const std::string& target,
    const std::string& body, ConfigureRequestFn&& configure_request) {
  constexpr int kMaxAttempts = 5;
  for (int i = 0; i < kMaxAttempts; ++i) {
    try {
      return DoRequestTask(method, port, target, body, configure_request);
    } catch (const std::exception&) {
      // A short startup race can happen between server thread start and accept.
      // Yield to avoid busy-spinning and to give the server time to bind.
      std::this_thread::yield();
    }
  }
  throw std::runtime_error("Failed to connect to test server after retries");
}

// Retry connection attempts for short startup races.
inline http::response<http::string_body> DoRequestTaskWithRetry(
    http::verb method, unsigned short port, const std::string& target,
    const std::string& body) {
  return DoRequestTaskWithRetry(method, port, target, body,
                                [](http::request<http::string_body>&) {});
}

// Keep compatibility with existing tests that use old helper function names.
template <typename ConfigureRequestFn>
inline http::response<http::string_body> DoRequestWithRetry(
    http::verb method, unsigned short port, const std::string& target,
    const std::string& body, ConfigureRequestFn&& configure_request) {
  return DoRequestTaskWithRetry(
      method, port, target, body,
      std::forward<ConfigureRequestFn>(configure_request));
}

inline http::response<http::string_body> DoRequestWithRetry(
    http::verb method, unsigned short port, const std::string& target,
    const std::string& body) {
  return DoRequestTaskWithRetry(method, port, target, body);
}

// Start server and retry if binding fails.
inline unsigned short StartServerWithRoutes(ServerGuard& guard) {
  constexpr int kMaxAttempts = 5;
  for (int i = 0; i < kMaxAttempts; ++i) {
    unsigned short port = FindFreePort();
    // Bind may still fail due to the race window (or OS reuse); retry a few
    // times.
    guard.server->AddListen({boost::asio::ip::make_address("127.0.0.1"), port},
                            1);
    if (guard.server->Start()) {
      return port;
    }
    guard.server->Stop();
  }
  throw std::runtime_error("Failed to start server after retries");
}

}  // namespace bsrvcore::test
