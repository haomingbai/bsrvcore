#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/websocket.hpp>
#include <cstddef>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "bsrvcore/connection/client/websocket_client_task.h"
#include "stress_test_common.h"

namespace {
namespace websocket = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;
using bsrvcore::test::stress::LoadStressConfig;
using bsrvcore::test::stress::WaitCounter;

class StressHandler : public bsrvcore::WebSocketHandler {
 public:
  explicit StressHandler(std::shared_ptr<std::atomic_size_t> open_counter,
                         std::shared_ptr<std::atomic_size_t> error_counter)
      : open_counter_(std::move(open_counter)),
        error_counter_(std::move(error_counter)) {}

  void OnOpen() override {
    open_counter_->fetch_add(1, std::memory_order_relaxed);
  }
  void OnReadMessage(const bsrvcore::WebSocketMessage&) override {}
  void OnError(boost::system::error_code, const std::string&) override {
    error_counter_->fetch_add(1, std::memory_order_relaxed);
  }
  void OnClose(boost::system::error_code) override {}

 private:
  std::shared_ptr<std::atomic_size_t> open_counter_;
  std::shared_ptr<std::atomic_size_t> error_counter_;
};

TEST(StressWebSocketTasksTest, Concurrent101HandshakeRemainsStable) {
  const auto cfg = LoadStressConfig(4, 40, 120000);
  const std::size_t total_accepts = cfg.threads * cfg.iterations;

  boost::asio::io_context server_ioc;
  tcp::acceptor acceptor(server_ioc, tcp::endpoint(tcp::v4(), 0));
  const auto port = acceptor.local_endpoint().port();

  std::jthread server_thread([&](std::stop_token st) {
    for (std::size_t i = 0; i < total_accepts && !st.stop_requested(); ++i) {
      tcp::socket socket(server_ioc);
      boost::system::error_code ec;
      acceptor.accept(socket, ec);
      if (ec) {
        continue;
      }

      websocket::stream<tcp::socket> ws(std::move(socket));
      ws.accept(ec);
      if (ec) {
        continue;
      }

      ws.close(websocket::close_code::normal, ec);
    }
  });

  std::barrier sync(static_cast<std::ptrdiff_t>(cfg.threads));
  WaitCounter done(cfg.threads);

  auto open_counter = std::make_shared<std::atomic_size_t>(0);
  auto error_counter = std::make_shared<std::atomic_size_t>(0);

  std::vector<std::jthread> workers;
  workers.reserve(cfg.threads);

  for (std::size_t t = 0; t < cfg.threads; ++t) {
    workers.emplace_back([&, t](std::stop_token st) {
      (void)t;
      sync.arrive_and_wait();

      for (std::size_t i = 0; i < cfg.iterations && !st.stop_requested(); ++i) {
        boost::asio::io_context ioc;
        auto task = bsrvcore::WebSocketClientTask::CreateHttp(
            ioc.get_executor(), "127.0.0.1", std::to_string(port), "/ws101",
            std::make_unique<StressHandler>(open_counter, error_counter));

        ASSERT_NE(task, nullptr);
        task->Start();
        ioc.run();
      }

      done.MarkOneDone();
    });
  }

  const bool completed = done.WaitFor(cfg.timeout);
  server_thread.request_stop();
  boost::system::error_code ignore_ec;
  acceptor.close(ignore_ec);

  if (!completed) {
    for (auto& th : workers) {
      th.request_stop();
    }
    ADD_FAILURE() << "Timeout waiting for workers. finished=" << done.Finished()
                  << "/" << cfg.threads;
  }

  EXPECT_GT(open_counter->load(std::memory_order_relaxed), 0U);
  EXPECT_EQ(error_counter->load(std::memory_order_relaxed), 0U);
}

}  // namespace
