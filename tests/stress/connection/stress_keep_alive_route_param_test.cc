#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "bsrvcore/connection/server/http_server_task.h"
#include "bsrvcore/core/http_server.h"
#include "bsrvcore/route/http_request_method.h"
#include "stress_test_common.h"
#include "test_http_client_task.h"

namespace {

namespace http = boost::beast::http;

class KeepAliveClient {
 public:
  KeepAliveClient(std::string host, unsigned short port)
      : host_(std::move(host)),
        port_(std::to_string(port)),
        host_header_(host_ + ":" + port_),
        resolver_(ioc_),
        stream_(ioc_) {}

  ~KeepAliveClient() { Close(); }

  http::response<http::string_body> Get(std::string target) {
    if (!connected_) {
      Connect();
    }

    http::request<http::string_body> request{http::verb::get, std::move(target),
                                             11};
    request.set(http::field::host, host_header_);
    request.set(http::field::user_agent,
                "bsrvcore-stress-keepalive-route-param");
    request.keep_alive(true);
    request.prepare_payload();

    stream_.expires_after(std::chrono::seconds(5));
    http::write(stream_, request);

    http::response<http::string_body> response;
    stream_.expires_after(std::chrono::seconds(5));
    http::read(stream_, buffer_, response);
    if (!response.keep_alive()) {
      Close();
    }
    return response;
  }

 private:
  void Connect() {
    const auto endpoints = resolver_.resolve(host_, port_);
    stream_.expires_after(std::chrono::seconds(2));
    stream_.connect(endpoints);
    boost::system::error_code ec;
    stream_.socket().set_option(boost::asio::ip::tcp::no_delay(true), ec);
    connected_ = true;
  }

  void Close() {
    if (!connected_) {
      buffer_.consume(buffer_.size());
      return;
    }
    boost::system::error_code ec;
    stream_.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    stream_.socket().close(ec);
    buffer_.consume(buffer_.size());
    connected_ = false;
  }

  std::string host_;
  std::string port_;
  std::string host_header_;
  bsrvcore::IoContext ioc_;
  boost::asio::ip::tcp::resolver resolver_;
  bsrvcore::TcpStream stream_;
  bsrvcore::FlatBuffer buffer_;
  bool connected_ = false;
};

bool HasHeader(const http::response<http::string_body>& response,
               std::string_view name) {
  for (const auto& field : response.base()) {
    if (boost::beast::iequals(field.name_string(), name)) {
      return true;
    }
  }
  return false;
}

struct StressConfig {
  std::size_t server_threads = 10;
  std::size_t client_workers = 40;
  std::size_t iterations = 200;
  std::chrono::milliseconds timeout{120000};
};

StressConfig LoadConfig() {
  StressConfig cfg;
  cfg.server_threads =
      bsrvcore::test::stress::GetEnvSize("BSRVCORE_STRESS_SERVER_THREADS", 10);
  cfg.client_workers =
      bsrvcore::test::stress::GetEnvSize("BSRVCORE_STRESS_THREADS", 40);
  cfg.iterations =
      bsrvcore::test::stress::GetEnvSize("BSRVCORE_STRESS_ITERATIONS", 200);
  cfg.timeout = std::chrono::milliseconds(
      bsrvcore::test::stress::GetEnvSize("BSRVCORE_STRESS_TIMEOUT_MS", 120000));
  return cfg;
}

}  // namespace

TEST(StressKeepAliveRouteParamTest,
     KeepAliveRouteParamAndAspectRemainResponsive) {
  const auto cfg = LoadConfig();
  SCOPED_TRACE(::testing::Message() << "server_threads=" << cfg.server_threads
                                    << " client_workers=" << cfg.client_workers
                                    << " iterations=" << cfg.iterations
                                    << " timeout_ms=" << cfg.timeout.count());

  bsrvcore::HttpServerExecutorOptions options;
  options.core_thread_num = cfg.server_threads;
  options.max_thread_num = cfg.server_threads;
  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(options);
  server->SetHeaderReadExpiry(5000)
      ->SetDefaultReadExpiry(5000)
      ->SetDefaultWriteExpiry(5000)
      ->SetKeepAliveTimeout(5000);
  server->AddGlobalAspect(
      [](const std::shared_ptr<bsrvcore::HttpPreServerTask>& task) {
        task->SetField("X-Stress-Aspect", "1");
      },
      [](const std::shared_ptr<bsrvcore::HttpPostServerTask>& task) {});
  server->AddRouteEntry(
      bsrvcore::HttpRequestMethod::kGet, "/users/{id}",
      [](const std::shared_ptr<bsrvcore::HttpServerTask>& task) {
        task->SetKeepAlive(task->GetRequest().keep_alive());
        task->GetResponse().result(http::status::ok);
        task->SetField(http::field::content_type, "text/plain; charset=utf-8");
        const auto* id = task->GetPathParameter("id");
        if (id == nullptr) {
          task->GetResponse().result(http::status::bad_request);
          task->SetBody("missing-id");
          return;
        }
        task->SetBody(*id);
      });

  bsrvcore::test::ServerGuard guard(std::move(server));
  const auto port = bsrvcore::test::FindFreePort();
  guard.server->AddListen({boost::asio::ip::make_address("127.0.0.1"), port},
                          cfg.server_threads);
  ASSERT_TRUE(guard.server->Start());

  std::atomic<std::size_t> failures{0};
  std::mutex error_mutex;
  std::vector<std::string> errors;

  const auto record_error = [&](std::string message) {
    failures.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(error_mutex);
    if (errors.size() < 32) {
      errors.push_back(std::move(message));
    }
  };

  bsrvcore::test::stress::WaitCounter done(cfg.client_workers);
  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.client_workers));
  std::vector<std::jthread> workers;
  workers.reserve(cfg.client_workers);

  for (std::size_t worker_index = 0; worker_index < cfg.client_workers;
       ++worker_index) {
    workers.emplace_back([&, worker_index](std::stop_token stop_token) {
      try {
        KeepAliveClient client("127.0.0.1", port);
        sync.arrive_and_wait();

        for (std::size_t i = 0;
             i < cfg.iterations && !stop_token.stop_requested(); ++i) {
          const std::string expected =
              "w" + std::to_string(worker_index) + "-" + std::to_string(i);
          const auto response = client.Get("/users/" + expected);
          if (response.result() != http::status::ok) {
            record_error(
                "unexpected status: " +
                std::to_string(static_cast<unsigned>(response.result())));
            break;
          }
          if (response.body() != expected) {
            record_error("route param mismatch");
            break;
          }
          if (!HasHeader(response, "X-Stress-Aspect")) {
            record_error("missing X-Stress-Aspect header");
            break;
          }
        }
      } catch (const std::exception& ex) {
        record_error(std::string("worker request failed: ") + ex.what());
      }
      done.MarkOneDone();
    });
  }

  const bool completed = done.WaitFor(cfg.timeout);
  if (!completed) {
    for (auto& worker : workers) {
      worker.request_stop();
    }
    ADD_FAILURE() << "Timeout waiting for keep-alive workers. finished="
                  << done.Finished() << "/" << cfg.client_workers;
  }

  for (auto& worker : workers) {
    worker.join();
  }

  if (!errors.empty()) {
    ADD_FAILURE() << "Encountered " << errors.size()
                  << " keep-alive failures; first: " << errors.front();
  }

  EXPECT_EQ(failures.load(std::memory_order_relaxed), 0u);
}
