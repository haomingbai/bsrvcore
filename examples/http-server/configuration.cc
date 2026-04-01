/**
 * @file example_configuration.cc
 * @brief Configure default limits and timeouts.
 *
 * Demonstrates:
 * - Default read/write expiry
 * - Default maximum body size
 * - Keep-alive timeout
 * - Session timeout and background cleaner
 *
 * Prerequisites: Boost, OpenSSL (required by bsrvcore build).
 * Build: cmake -S . -B build -DBSRVCORE_BUILD_EXAMPLES=ON
 * Run: ./build/examples/example_configuration
 */

// BEGIN README_SNIPPET: configuration
#include <bsrvcore/bsrvcore.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <iostream>
#include <memory>

int main() {
  bsrvcore::HttpServerRuntimeOptions options;
  options.core_thread_num = 2;
  options.max_thread_num = 2;
  options.fast_queue_capacity = 128;
  options.thread_clean_interval = 60000;
  options.task_scan_interval = 100;
  options.suspend_time = 1;
  options.has_max_connection = true;
  options.max_connection = 1024;

  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(options);
  server->SetDefaultReadExpiry(5000)
      ->SetDefaultWriteExpiry(5000)
      ->SetDefaultMaxBodySize(1024 * 1024)
      ->SetKeepAliveTimeout(15000)
      ->SetDefaultSessionTimeout(10 * 60 * 1000)
      ->SetSessionCleaner(true)
      ->AddRouteEntry(
          bsrvcore::HttpRequestMethod::kGet, "/config",
          [](std::shared_ptr<bsrvcore::HttpServerTask> task) {
            task->GetResponse().result(boost::beast::http::status::ok);
            task->SetField(boost::beast::http::field::content_type,
                           "text/plain; charset=utf-8");
            task->SetBody("Default limits and timeouts are configured.\n");
          })
      ->AddListen({boost::asio::ip::make_address("0.0.0.0"), 8081}, 1);

  if (!server->Start()) {
    std::cerr << "failed to start server" << std::endl;
    return 1;
  }

  std::cout << "Listening on http://0.0.0.0:8081/config" << std::endl;
  std::cout << "Press Enter to stop." << std::endl;
  std::cin.get();

  server->Stop();
  return 0;
}
// END README_SNIPPET: configuration
