#include "benchmark_client.h"

#include <boost/beast/http.hpp>
#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "benchmark_util.h"

namespace {

namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;

constexpr auto kConnectTimeout = std::chrono::seconds(2);
constexpr auto kRequestTimeout = std::chrono::seconds(10);
constexpr std::string_view kBenchmarkUserAgent = "bsrvcore-http-benchmark";

std::uint64_t ApproximateRequestBytes(
    const bsrvcore::benchmark::http::request<
        bsrvcore::benchmark::http::string_body>& request) {
  std::uint64_t total = request.method_string().size() + 1 +
                        request.target().size() + 1 +
                        std::string_view("HTTP/1.1").size() + 2;
  for (const auto& field : request.base()) {
    total += field.name_string().size() + 2 + field.value().size() + 2;
  }
  total += 2;
  total += request.body().size();
  return total;
}

std::uint64_t ApproximateResponseBytes(
    const bsrvcore::benchmark::BenchmarkHttpResponse& response) {
  std::uint64_t total = std::string_view("HTTP/1.1").size() + 1 + 3 + 1 +
                        response.reason().size() + 2;
  for (const auto& field : response.base()) {
    total += field.name_string().size() + 2 + field.value().size() + 2;
  }
  total += 2;
  total += response.body().size();
  return total;
}

std::runtime_error MakeIoError(std::string_view operation,
                               const boost::system::error_code& ec) {
  return std::runtime_error(std::string(operation) +
                            " failed: " + ec.message());
}

template <typename Initiate>
boost::system::error_code RunAsyncOperation(boost::asio::io_context& ioc,
                                            Initiate&& initiate) {
  boost::system::error_code ec;
  bool done = false;

  ioc.restart();
  initiate([&](boost::system::error_code operation_ec, auto&&...) {
    ec = operation_ec;
    done = true;
  });

  while (!done) {
    if (ioc.run_one() == 0) {
      throw std::runtime_error(
          "benchmark client asynchronous operation made no progress");
    }
  }

  return ec;
}

}  // namespace

namespace bsrvcore::benchmark {

KeepAliveClient::KeepAliveClient(std::string host, unsigned short port)
    : host_(std::move(host)),
      port_(std::to_string(port)),
      host_header_(host_ + ":" + port_),
      resolver_(ioc_),
      stream_(ioc_) {}

KeepAliveClient::~KeepAliveClient() { Close(); }

void KeepAliveClient::Connect() {
  boost::system::error_code ec;
  const auto endpoints = resolver_.resolve(host_, port_, ec);
  if (ec) {
    throw MakeIoError("resolve", ec);
  }

  stream_.expires_after(kConnectTimeout);
  ec = RunAsyncOperation(ioc_, [&](auto&& handler) {
    stream_.async_connect(endpoints, std::forward<decltype(handler)>(handler));
  });
  if (ec) {
    throw MakeIoError("connect", ec);
  }

  stream_.socket().set_option(tcp::no_delay(true), ec);
  if (ec) {
    Close();
    throw MakeIoError("set TCP_NODELAY", ec);
  }

  connected_ = true;
}

ExchangeResult KeepAliveClient::Send(
    const RequestSpec& spec, std::map<std::string, std::string>& cookie_jar) {
  if (!connected_) {
    Connect();
  }

  http::request<http::string_body> request{spec.method, spec.target, 11};
  request.set(http::field::host, host_header_);
  request.set(http::field::user_agent, kBenchmarkUserAgent);
  request.set(http::field::accept, "*/*");
  request.keep_alive(spec.keep_alive);

  if (!cookie_jar.empty()) {
    request.set(http::field::cookie, BuildCookieHeader(cookie_jar));
  }
  for (const auto& [field, value] : spec.headers) {
    request.set(field, value);
  }
  request.body() = spec.body;
  request.prepare_payload();

  try {
    auto ec = boost::system::error_code{};
    stream_.expires_after(kRequestTimeout);
    ec = RunAsyncOperation(ioc_, [&](auto&& handler) {
      http::async_write(stream_, request,
                        std::forward<decltype(handler)>(handler));
    });
    if (ec) {
      throw MakeIoError("http write", ec);
    }

    BenchmarkHttpResponse response;
    stream_.expires_after(kRequestTimeout);
    ec = RunAsyncOperation(ioc_, [&](auto&& handler) {
      http::async_read(stream_, buffer_, response,
                       std::forward<decltype(handler)>(handler));
    });
    if (ec) {
      throw MakeIoError("http read", ec);
    }

    SyncCookieJar(response.base(), cookie_jar);
    if (!response.keep_alive()) {
      Close();
    }

    return {std::move(response), ApproximateRequestBytes(request),
            ApproximateResponseBytes(response)};
  } catch (...) {
    Close();
    throw;
  }
}

void KeepAliveClient::Close() {
  if (!connected_) {
    buffer_.consume(buffer_.size());
    return;
  }

  boost::system::error_code ec;
  stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
  stream_.socket().close(ec);
  buffer_.consume(buffer_.size());
  connected_ = false;
}

}  // namespace bsrvcore::benchmark
