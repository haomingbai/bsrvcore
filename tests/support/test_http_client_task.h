#pragma once

#include <boost/asio/ip/address.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "bsrvcore/http_client_task.h"
#include "bsrvcore/http_server.h"

namespace bsrvcore::test {

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;

// Bind to port 0 to get an available ephemeral port.
inline unsigned short FindFreePort() {
  boost::asio::io_context ioc;
  tcp::acceptor acceptor(ioc, {boost::asio::ip::make_address("127.0.0.1"), 0});
  return acceptor.local_endpoint().port();
}

// RAII guard to stop the server on scope exit.
struct ServerGuard {
  explicit ServerGuard(std::unique_ptr<bsrvcore::HttpServer> srv)
      : server(std::move(srv)) {}

  ~ServerGuard() {
    if (server) {
      server->Stop();
    }
  }

  std::unique_ptr<bsrvcore::HttpServer> server;
};

inline http::response<http::string_body> DoRequestTask(
    http::verb method, unsigned short port, const std::string& target,
    const std::string& body = "") {
  boost::asio::io_context ioc;

  HttpClientOptions options;
  options.resolve_timeout = std::chrono::seconds(2);
  options.connect_timeout = std::chrono::seconds(2);
  options.write_timeout = std::chrono::seconds(2);
  options.read_header_timeout = std::chrono::seconds(2);
  options.read_body_timeout = std::chrono::seconds(2);
  options.user_agent = "bsrvcore-test-client-task";

  auto task = HttpClientTask::CreateHttp(
      ioc.get_executor(), "127.0.0.1", std::to_string(port), target, method,
      options);

  task->Request().body() = body;

  std::promise<http::response<http::string_body>> promise;
  auto future = promise.get_future();

  task->OnDone([&promise](const HttpClientResult& result) mutable {
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
  ioc.run();

  return future.get();
}

// Retry connection attempts for short startup races.
inline http::response<http::string_body> DoRequestTaskWithRetry(
    http::verb method, unsigned short port, const std::string& target,
    const std::string& body) {
  constexpr int kMaxAttempts = 5;
  for (int i = 0; i < kMaxAttempts; ++i) {
    try {
      return DoRequestTask(method, port, target, body);
    } catch (const std::exception&) {
      std::this_thread::yield();
    }
  }
  throw std::runtime_error("Failed to connect to test server after retries");
}

// Keep compatibility with existing tests that use old helper function names.
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
    guard.server->AddListen({boost::asio::ip::make_address("127.0.0.1"), port});
    if (guard.server->Start(1)) {
      return port;
    }
    guard.server->Stop();
  }
  throw std::runtime_error("Failed to start server after retries");
}

}  // namespace bsrvcore::test
