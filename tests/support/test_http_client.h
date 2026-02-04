#pragma once

#include <boost/asio/ip/address.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

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

// Send a single HTTP request to the test server.
inline http::response<http::string_body> DoRequest(http::verb method,
                                                   unsigned short port,
                                                   const std::string& target,
                                                   const std::string& body = "") {
  boost::asio::io_context ioc;
  tcp::resolver resolver(ioc);
  boost::beast::tcp_stream stream(ioc);

  stream.expires_after(std::chrono::seconds(2));
  auto const results = resolver.resolve("127.0.0.1", std::to_string(port));
  boost::system::error_code ec;
  stream.connect(results, ec);
  if (ec) {
    throw boost::system::system_error(ec);
  }

  http::request<http::string_body> req{method, target, 11};
  req.set(http::field::host, "127.0.0.1");
  req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
  req.body() = body;
  req.keep_alive(false);
  req.prepare_payload();

  stream.expires_after(std::chrono::seconds(2));
  http::write(stream, req);

  boost::beast::flat_buffer buffer;
  http::response<http::string_body> res;
  stream.expires_after(std::chrono::seconds(2));
  http::read(stream, buffer, res);

  stream.socket().shutdown(tcp::socket::shutdown_both, ec);
  return res;
}

// Retry connection attempts for short startup races.
inline http::response<http::string_body> DoRequestWithRetry(
    http::verb method, unsigned short port, const std::string& target,
    const std::string& body) {
  constexpr int kMaxAttempts = 5;
  for (int i = 0; i < kMaxAttempts; ++i) {
    try {
      return DoRequest(method, port, target, body);
    } catch (const boost::system::system_error&) {
      std::this_thread::yield();
    }
  }
  throw std::runtime_error("Failed to connect to test server after retries");
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
