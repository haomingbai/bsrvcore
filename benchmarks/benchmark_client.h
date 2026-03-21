#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/basic_resolver.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <cstdint>
#include <map>
#include <string>

#include "benchmark_types.h"

namespace bsrvcore::benchmark {

struct ExchangeResult {
  BenchmarkHttpResponse response;
  std::uint64_t bytes_sent = 0;
  std::uint64_t bytes_received = 0;
};

class KeepAliveClient {
 public:
  KeepAliveClient(std::string host, unsigned short port);
  ~KeepAliveClient();

  void Connect();
  ExchangeResult Send(const RequestSpec& spec,
                      std::map<std::string, std::string>& cookie_jar);
  void Close();

 private:
  std::string host_;
  std::string port_;
  std::string host_header_;
  boost::asio::io_context ioc_;
  boost::asio::ip::tcp::resolver resolver_;
  boost::beast::tcp_stream stream_;
  boost::beast::flat_buffer buffer_;
  bool connected_ = false;
};

}  // namespace bsrvcore::benchmark
