#include "benchmark_runner.h"

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "benchmark_client.h"
#include "benchmark_subprocess.h"
#include "benchmark_util.h"
#include "bsrvcore/http_server.h"

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

constexpr std::size_t kServerStartRetries = 8;
constexpr auto kServerDrainDelay = std::chrono::milliseconds(20);

using Clock = std::chrono::steady_clock;

struct WorkerMetrics {
  std::uint64_t success_count = 0;
  std::uint64_t error_count = 0;
  std::uint64_t bytes_sent = 0;
  std::uint64_t bytes_received = 0;
  std::vector<std::uint32_t> latencies_us;
  std::string error_message;
};

enum class PhaseKind { kWarmup, kMeasure, kCooldown, kStop };

bool TraceEnabled() {
  static const bool enabled = std::getenv("BSRVCORE_BENCHMARK_TRACE") != nullptr;
  return enabled;
}

void Trace(std::string_view message) {
  if (TraceEnabled()) {
    std::cerr << "[trace] " << message << "\n";
  }
}

std::uint64_t ToSteadyNs(Clock::time_point tp) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          tp.time_since_epoch())
          .count());
}

unsigned short FindFreePort() {
  asio::io_context ioc;
  tcp::acceptor acceptor(ioc, {asio::ip::make_address("127.0.0.1"), 0});
  return acceptor.local_endpoint().port();
}

class ServerGuard {
 public:
  explicit ServerGuard(std::unique_ptr<bsrvcore::HttpServer> server)
      : server_(std::move(server)) {}

  ~ServerGuard() {
    if (server_) {
      server_->Stop();
    }
  }

 private:
  std::unique_ptr<bsrvcore::HttpServer> server_;
};

struct StartedServer {
  std::unique_ptr<ServerGuard> guard;
  unsigned short port = 0;
};

std::unique_ptr<bsrvcore::HttpServer> BuildServer(
    const bsrvcore::benchmark::ScenarioDefinition& scenario,
    std::size_t server_threads) {
  bsrvcore::HttpServerExecutorOptions options;
  options.core_thread_num = server_threads;
  options.max_thread_num = server_threads;

  auto server = std::make_unique<bsrvcore::HttpServer>(options);
  server->SetHeaderReadExpiry(5000)
      ->SetDefaultReadExpiry(5000)
      ->SetDefaultWriteExpiry(5000)
      ->SetKeepAliveTimeout(5000)
      ->SetDefaultMaxBodySize(
          std::max<std::size_t>(scenario.required_max_body_size, 256 * 1024));
  scenario.configure_server(*server);
  return server;
}

StartedServer StartServerWithRetry(
    const bsrvcore::benchmark::ScenarioDefinition& scenario,
    std::size_t server_threads) {
  std::string last_error = "unknown";
  for (std::size_t attempt = 0; attempt < kServerStartRetries; ++attempt) {
    auto server = BuildServer(scenario, server_threads);
    try {
      const auto port = FindFreePort();
      server->AddListen({asio::ip::make_address("127.0.0.1"), port});
      if (!server->Start(server_threads)) {
        last_error = "HttpServer::Start returned false";
        server->Stop();
        continue;
      }
      return {std::make_unique<ServerGuard>(std::move(server)), port};
    } catch (const std::exception& ex) {
      last_error = ex.what();
      if (server) {
        server->Stop();
      }
    } catch (...) {
      last_error = "unknown non-std exception";
      if (server) {
        server->Stop();
      }
    }
  }

  throw std::runtime_error("Failed to start benchmark server after retries: " +
                           last_error);
}

void ValidateScenario(const bsrvcore::benchmark::ScenarioDefinition& scenario,
                      unsigned short port) {
  bsrvcore::benchmark::WorkerState state;
  bsrvcore::benchmark::KeepAliveClient client("127.0.0.1", port);
  auto request = scenario.make_request(state);
  auto result = client.Send(request, state.cookie_jar);
  std::string error;
  if (!scenario.validate_response(result.response, state, error)) {
    throw std::runtime_error("validation failed for " + scenario.name + ": " +
                             error);
  }

  client.Close();
}

}  // namespace

namespace bsrvcore::benchmark {

RepetitionMetrics RunCellRepetition(const ScenarioDefinition& scenario,
                                    const PressureSettings& pressure,
                                    std::size_t warmup_ms,
                                    std::size_t duration_ms,
                                    std::size_t cooldown_ms,
                                    std::size_t repetition) {
  Trace(std::string("start cell ") + scenario.name + "/" + pressure.name);
  auto started_server = StartServerWithRetry(scenario, pressure.server_threads);
  Trace(std::string("server started ") + scenario.name + "/" + pressure.name);
  ValidateScenario(scenario, started_server.port);
  Trace(std::string("validation ok ") + scenario.name + "/" + pressure.name);

  std::barrier sync(static_cast<std::ptrdiff_t>(pressure.client_concurrency + 1));
  std::atomic<PhaseKind> current_phase{PhaseKind::kWarmup};
  std::atomic<std::uint64_t> phase_end_ns{0};
  std::atomic<bool> stop_requested{false};

  std::vector<WorkerMetrics> worker_metrics(pressure.client_concurrency);
  std::vector<std::jthread> workers;
  workers.reserve(pressure.client_concurrency);

  for (std::size_t index = 0; index < pressure.client_concurrency; ++index) {
    workers.emplace_back([&, index](std::stop_token) {
      WorkerMetrics& metrics = worker_metrics[index];
      bsrvcore::benchmark::WorkerState state;
      state.worker_index = index;

      auto mark_error = [&](std::string message) {
        ++metrics.error_count;
        if (metrics.error_message.empty()) {
          metrics.error_message = std::move(message);
        }
        stop_requested.store(true, std::memory_order_relaxed);
      };

      std::optional<bsrvcore::benchmark::KeepAliveClient> client;
      try {
        client.emplace("127.0.0.1", started_server.port);
        client->Connect();
        if (scenario.prime_each_worker) {
          auto request = scenario.make_request(state);
          auto result = client->Send(request, state.cookie_jar);
          std::string error;
          if (!scenario.validate_response(result.response, state, error)) {
            mark_error("prime failed: " + error);
          }
        }
      } catch (const std::exception& ex) {
        mark_error(ex.what());
      }

      sync.arrive_and_wait();

      while (true) {
        sync.arrive_and_wait();
        const PhaseKind phase = current_phase.load(std::memory_order_relaxed);
        if (phase == PhaseKind::kStop) {
          break;
        }

        if (metrics.error_message.empty()) {
          while (!stop_requested.load(std::memory_order_relaxed) &&
                 ToSteadyNs(Clock::now()) <
                     phase_end_ns.load(std::memory_order_relaxed)) {
            try {
              const auto request = scenario.make_request(state);
              const auto start = Clock::now();
              auto result = client->Send(request, state.cookie_jar);
              const auto end = Clock::now();

              std::string error;
              if (!scenario.validate_response(result.response, state, error)) {
                mark_error("response validation failed: " + error);
                break;
              }

              if (phase == PhaseKind::kMeasure) {
                ++metrics.success_count;
                metrics.bytes_sent += result.bytes_sent;
                metrics.bytes_received += result.bytes_received;
                const auto latency =
                    std::chrono::duration_cast<std::chrono::microseconds>(end -
                                                                          start)
                        .count();
                metrics.latencies_us.push_back(static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(
                        latency, 0,
                        static_cast<std::int64_t>(
                            std::numeric_limits<std::uint32_t>::max()))));
              }
            } catch (const std::exception& ex) {
              mark_error(ex.what());
              break;
            }
          }
        }

        sync.arrive_and_wait();
      }

      if (client.has_value()) {
        client->Close();
      }
    });
  }

  sync.arrive_and_wait();
  Trace(std::string("workers ready ") + scenario.name + "/" + pressure.name);

  auto run_phase = [&](PhaseKind phase, std::size_t duration_ms_for_phase) {
    current_phase.store(phase, std::memory_order_relaxed);
    phase_end_ns.store(
        ToSteadyNs(Clock::now() + std::chrono::milliseconds(duration_ms_for_phase)),
        std::memory_order_relaxed);
    sync.arrive_and_wait();
    sync.arrive_and_wait();
  };

  run_phase(PhaseKind::kWarmup, warmup_ms);
  Trace(std::string("warmup done ") + scenario.name + "/" + pressure.name);

  auto measurement_start = Clock::now();
  auto measurement_end = measurement_start;
  if (!stop_requested.load(std::memory_order_relaxed)) {
    measurement_start = Clock::now();
    run_phase(PhaseKind::kMeasure, duration_ms);
    measurement_end = Clock::now();
    Trace(std::string("measure done ") + scenario.name + "/" + pressure.name);
  }
  if (!stop_requested.load(std::memory_order_relaxed)) {
    run_phase(PhaseKind::kCooldown, cooldown_ms);
    Trace(std::string("cooldown done ") + scenario.name + "/" + pressure.name);
  }

  current_phase.store(PhaseKind::kStop, std::memory_order_relaxed);
  sync.arrive_and_wait();
  Trace(std::string("workers stopping ") + scenario.name + "/" + pressure.name);
  workers.clear();
  Trace(std::string("workers joined ") + scenario.name + "/" + pressure.name);
  std::this_thread::sleep_for(kServerDrainDelay);
  Trace(std::string("server drain waited ") + scenario.name + "/" +
        pressure.name);
  started_server.guard.reset();
  Trace(std::string("server stopped ") + scenario.name + "/" + pressure.name);

  RepetitionMetrics metrics;
  metrics.repetition = repetition;
  metrics.duration_seconds =
      std::chrono::duration_cast<std::chrono::duration<double>>(
          measurement_end - measurement_start)
          .count();

  std::vector<std::uint32_t> latencies;
  for (const auto& worker : worker_metrics) {
    metrics.success_count += worker.success_count;
    metrics.error_count += worker.error_count;
    metrics.bytes_sent += worker.bytes_sent;
    metrics.bytes_received += worker.bytes_received;
    latencies.insert(latencies.end(), worker.latencies_us.begin(),
                     worker.latencies_us.end());
    if (!worker.error_message.empty()) {
      throw std::runtime_error("repetition " + std::to_string(repetition) +
                               " failed in scenario " + scenario.name + "/" +
                               pressure.name + ": " + worker.error_message);
    }
  }

  if (metrics.duration_seconds > 0.0) {
    metrics.requests_per_second =
        static_cast<double>(metrics.success_count) / metrics.duration_seconds;
    metrics.mib_per_second =
        static_cast<double>(metrics.bytes_sent + metrics.bytes_received) /
        (1024.0 * 1024.0) / metrics.duration_seconds;
  }

  latencies = SampleLatencies(latencies);
  std::sort(latencies.begin(), latencies.end());
  metrics.latency_p50_us = PercentileFromSorted(latencies, 0.50);
  metrics.latency_p95_us = PercentileFromSorted(latencies, 0.95);
  metrics.latency_p99_us = PercentileFromSorted(latencies, 0.99);
  metrics.latency_max_us =
      latencies.empty() ? 0.0 : static_cast<double>(latencies.back());
  Trace(std::string("cell complete ") + scenario.name + "/" + pressure.name);

  return metrics;
}

std::vector<CellResult> RunBenchmarks(
    const std::filesystem::path& executable_path,
    const std::vector<const ScenarioDefinition*>& selected_scenarios,
    const RunSettings& run_settings, ProfileKind profile) {
  std::unordered_map<std::string, CellResult> cells_by_key;
  std::vector<std::string> cell_order;

  for (std::size_t repetition = 0; repetition < run_settings.repetitions;
       ++repetition) {
    std::vector<const ScenarioDefinition*> ordered = selected_scenarios;
    if (profile == ProfileKind::kFull && ordered.size() > 1) {
      std::rotate(ordered.begin(), ordered.begin() + repetition % ordered.size(),
                  ordered.end());
    }

    for (const ScenarioDefinition* scenario : ordered) {
      for (const auto& pressure : run_settings.pressures) {
        std::cout << "Running " << scenario->name << " under " << pressure.name
                  << " (rep " << (repetition + 1) << "/"
                  << run_settings.repetitions << ", server_threads="
                  << pressure.server_threads << ", client_concurrency="
                  << pressure.client_concurrency << ")\n";

        auto metrics = RunCellInSubprocess(
            executable_path, *scenario, pressure, run_settings.warmup_ms,
            run_settings.duration_ms, run_settings.cooldown_ms,
            repetition + 1);
        std::cout << "Finished " << scenario->name << " under " << pressure.name
                  << " (rep " << (repetition + 1) << "/"
                  << run_settings.repetitions << ")\n";

        const auto key = CellKey(scenario->name, pressure.name);
        auto [it, inserted] = cells_by_key.emplace(
            key, CellResult{scenario->name,
                            pressure.name,
                            pressure.server_threads,
                            pressure.client_concurrency,
                            run_settings.warmup_ms,
                            run_settings.duration_ms,
                            run_settings.repetitions,
                            run_settings.cooldown_ms,
                            {},
                            {}});
        if (inserted) {
          cell_order.push_back(key);
        }
        it->second.runs.push_back(std::move(metrics));
      }
    }
  }

  std::vector<CellResult> cells;
  cells.reserve(cell_order.size());
  for (const auto& key : cell_order) {
    auto cell = cells_by_key.at(key);
    cell.aggregate = AggregateRuns(cell.runs);
    cells.push_back(std::move(cell));
  }

  return cells;
}

}  // namespace bsrvcore::benchmark
