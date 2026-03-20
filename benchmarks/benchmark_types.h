#pragma once

#include <boost/beast/http.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bsrvcore {
class HttpServer;
}

namespace bsrvcore::benchmark {

namespace http = boost::beast::http;

using BenchmarkHttpResponse = http::response<http::string_body>;

enum class ProfileKind { kQuick, kFull };
enum class PressureKind { kLight, kBalanced, kSaturated, kOverload, kCustom };

struct CliConfig {
  bool list_scenarios = false;
  bool show_help = false;
  std::string scenario_name = "all";
  ProfileKind profile = ProfileKind::kQuick;
  std::optional<std::string> pressure_name;
  std::optional<std::size_t> server_threads_override;
  std::optional<std::size_t> client_concurrency_override;
  std::optional<std::size_t> warmup_ms_override;
  std::optional<std::size_t> duration_ms_override;
  std::optional<std::size_t> repetitions_override;
  std::optional<std::size_t> cooldown_ms_override;
  std::optional<std::filesystem::path> output_json;
  bool internal_run_cell = false;
  std::optional<std::string> internal_scenario_name;
  std::optional<std::string> internal_pressure_name;
  std::optional<std::size_t> internal_server_threads;
  std::optional<std::size_t> internal_client_concurrency;
  std::optional<std::size_t> internal_warmup_ms;
  std::optional<std::size_t> internal_duration_ms;
  std::optional<std::size_t> internal_cooldown_ms;
  std::optional<std::size_t> internal_repetition;
  std::optional<std::filesystem::path> internal_result_path;
};

struct ProfileSettings {
  std::vector<PressureKind> pressures;
  std::size_t warmup_ms = 0;
  std::size_t duration_ms = 0;
  std::size_t repetitions = 0;
  std::size_t cooldown_ms = 0;
};

struct PressureSettings {
  PressureKind kind = PressureKind::kCustom;
  std::string name;
  std::size_t server_threads = 1;
  std::size_t client_concurrency = 1;
};

struct RunSettings {
  std::vector<PressureSettings> pressures;
  std::size_t warmup_ms = 0;
  std::size_t duration_ms = 0;
  std::size_t repetitions = 0;
  std::size_t cooldown_ms = 0;
};

struct WorkerState {
  std::size_t worker_index = 0;
  std::uint64_t request_index = 0;
  std::map<std::string, std::string> cookie_jar;
};

struct RequestSpec {
  http::verb method = http::verb::get;
  std::string target = "/";
  std::string body;
  bool keep_alive = true;
  std::vector<std::pair<http::field, std::string>> headers;
};

struct ScenarioDefinition {
  std::string name;
  std::string summary;
  std::size_t required_max_body_size = 256 * 1024;
  bool prime_each_worker = false;
  std::function<void(HttpServer&)> configure_server;
  std::function<RequestSpec(WorkerState&)> make_request;
  std::function<bool(const BenchmarkHttpResponse&, WorkerState&, std::string&)>
      validate_response;
};

struct EnvironmentInfo {
  std::string timestamp_utc;
  std::string os;
  std::string compiler;
  std::string build_type;
  std::size_t logical_cpu_count = 1;
};

struct RepetitionMetrics {
  std::size_t repetition = 0;
  std::uint64_t success_count = 0;
  std::uint64_t error_count = 0;
  std::uint64_t bytes_sent = 0;
  std::uint64_t bytes_received = 0;
  double duration_seconds = 0.0;
  double requests_per_second = 0.0;
  double mib_per_second = 0.0;
  double latency_p50_us = 0.0;
  double latency_p95_us = 0.0;
  double latency_p99_us = 0.0;
  double latency_max_us = 0.0;
};

struct ScalarSummary {
  double median = 0.0;
  double mean = 0.0;
  double min = 0.0;
  double max = 0.0;
  double stdev = 0.0;
  double cv = 0.0;
};

struct AggregateMetrics {
  ScalarSummary success_count;
  ScalarSummary error_count;
  ScalarSummary bytes_sent;
  ScalarSummary bytes_received;
  ScalarSummary requests_per_second;
  ScalarSummary mib_per_second;
  ScalarSummary latency_p50_us;
  ScalarSummary latency_p95_us;
  ScalarSummary latency_p99_us;
  ScalarSummary latency_max_us;
  std::string stability = "stable";
};

struct CellResult {
  std::string scenario_name;
  std::string pressure_name;
  std::size_t server_threads = 1;
  std::size_t client_concurrency = 1;
  std::size_t warmup_ms = 0;
  std::size_t duration_ms = 0;
  std::size_t repetitions = 0;
  std::size_t cooldown_ms = 0;
  std::vector<RepetitionMetrics> runs;
  AggregateMetrics aggregate;
};

}  // namespace bsrvcore::benchmark
