#include "benchmark_report.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "benchmark_util.h"

namespace bsrvcore::benchmark {

namespace {

std::string JsonScalarSummary(const ScalarSummary& summary) {
  std::ostringstream out;
  out << "{"
      << "\"median\":" << FormatDouble(summary.median) << ','
      << "\"mean\":" << FormatDouble(summary.mean) << ','
      << "\"min\":" << FormatDouble(summary.min) << ','
      << "\"max\":" << FormatDouble(summary.max) << ','
      << "\"stdev\":" << FormatDouble(summary.stdev) << ','
      << "\"cv\":" << FormatDouble(summary.cv) << "}";
  return out.str();
}

std::string JsonRepetition(const RepetitionMetrics& run) {
  std::ostringstream out;
  out << "{"
      << "\"repetition\":" << run.repetition << ','
      << "\"attempt_count\":" << run.attempt_count << ','
      << "\"success_count\":" << run.success_count << ','
      << "\"error_count\":" << run.error_count << ','
      << "\"non_2xx_3xx_count\":" << run.non_2xx_3xx_count << ','
      << "\"socket_connect_error_count\":" << run.socket_connect_error_count
      << ',' << "\"socket_read_error_count\":" << run.socket_read_error_count
      << ',' << "\"socket_write_error_count\":" << run.socket_write_error_count
      << ','
      << "\"socket_timeout_error_count\":" << run.socket_timeout_error_count
      << ',' << "\"loadgen_failure_count\":" << run.loadgen_failure_count << ','
      << "\"bytes_sent\":" << run.bytes_sent << ','
      << "\"bytes_received\":" << run.bytes_received << ','
      << "\"duration_seconds\":" << FormatDouble(run.duration_seconds) << ','
      << "\"attempt_rps\":" << FormatDouble(run.attempt_requests_per_second)
      << ',' << "\"rps\":" << FormatDouble(run.requests_per_second) << ','
      << "\"failure_ratio\":" << FormatDouble(run.failure_ratio) << ','
      << "\"mib_per_sec\":" << FormatDouble(run.mib_per_second) << ','
      << "\"latency_us\":{"
      << "\"p50\":" << FormatDouble(run.latency_p50_us) << ','
      << "\"p95\":" << FormatDouble(run.latency_p95_us) << ','
      << "\"p99\":" << FormatDouble(run.latency_p99_us) << ','
      << "\"max\":" << FormatDouble(run.latency_max_us) << "}"
      << "}";
  return out.str();
}

std::string JsonCell(const CellResult& cell) {
  std::ostringstream out;
  out << "{"
      << "\"scenario\":\"" << EscapeJson(cell.scenario_name) << "\","
      << "\"pressure\":\"" << EscapeJson(cell.pressure_name) << "\","
      << "\"http_method\":\"" << EscapeJson(cell.http_method) << "\","
      << "\"server_io_threads\":" << cell.server_io_threads << ','
      << "\"server_worker_threads\":" << cell.server_worker_threads << ','
      << "\"client_concurrency\":" << cell.client_concurrency << ','
      << "\"request_body_bytes\":" << cell.request_body_bytes << ','
      << "\"response_body_bytes\":" << cell.response_body_bytes << ','
      << "\"warmup_ms\":" << cell.warmup_ms << ','
      << "\"duration_ms\":" << cell.duration_ms << ','
      << "\"repetitions\":" << cell.repetitions << ','
      << "\"cooldown_ms\":" << cell.cooldown_ms << ',' << "\"runs\":[";
  for (std::size_t i = 0; i < cell.runs.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    out << JsonRepetition(cell.runs[i]);
  }
  out << "],"
      << "\"aggregate\":{"
      << "\"attempt_count\":" << JsonScalarSummary(cell.aggregate.attempt_count)
      << ','
      << "\"success_count\":" << JsonScalarSummary(cell.aggregate.success_count)
      << ','
      << "\"error_count\":" << JsonScalarSummary(cell.aggregate.error_count)
      << ',' << "\"non_2xx_3xx_count\":"
      << JsonScalarSummary(cell.aggregate.non_2xx_3xx_count) << ','
      << "\"socket_connect_error_count\":"
      << JsonScalarSummary(cell.aggregate.socket_connect_error_count) << ','
      << "\"socket_read_error_count\":"
      << JsonScalarSummary(cell.aggregate.socket_read_error_count) << ','
      << "\"socket_write_error_count\":"
      << JsonScalarSummary(cell.aggregate.socket_write_error_count) << ','
      << "\"socket_timeout_error_count\":"
      << JsonScalarSummary(cell.aggregate.socket_timeout_error_count) << ','
      << "\"loadgen_failure_count\":"
      << JsonScalarSummary(cell.aggregate.loadgen_failure_count) << ','
      << "\"bytes_sent\":" << JsonScalarSummary(cell.aggregate.bytes_sent)
      << ',' << "\"bytes_received\":"
      << JsonScalarSummary(cell.aggregate.bytes_received) << ','
      << "\"attempt_rps\":"
      << JsonScalarSummary(cell.aggregate.attempt_requests_per_second) << ','
      << "\"rps\":" << JsonScalarSummary(cell.aggregate.requests_per_second)
      << ','
      << "\"failure_ratio\":" << JsonScalarSummary(cell.aggregate.failure_ratio)
      << ','
      << "\"mib_per_sec\":" << JsonScalarSummary(cell.aggregate.mib_per_second)
      << ',' << "\"latency_us\":{"
      << "\"p50\":" << JsonScalarSummary(cell.aggregate.latency_p50_us) << ','
      << "\"p95\":" << JsonScalarSummary(cell.aggregate.latency_p95_us) << ','
      << "\"p99\":" << JsonScalarSummary(cell.aggregate.latency_p99_us) << ','
      << "\"max\":" << JsonScalarSummary(cell.aggregate.latency_max_us) << "},"
      << "\"stability\":\"" << cell.aggregate.stability << "\""
      << "}"
      << "}";
  return out.str();
}

}  // namespace

void PrintScenarioList(const std::vector<ScenarioDefinition>& scenarios) {
  for (const auto& scenario : scenarios) {
    std::cout << scenario.name << " - " << scenario.summary << "\n";
  }
}

void PrintCellSummary(const CellResult& cell) {
  std::cout << "[" << cell.scenario_name << "/" << cell.pressure_name << "] "
            << "http_method=" << cell.http_method
            << " request_body_bytes=" << cell.request_body_bytes
            << " response_body_bytes=" << cell.response_body_bytes
            << " "
            << "server_io_threads=" << cell.server_io_threads
            << " server_worker_threads=" << cell.server_worker_threads
            << " client_concurrency=" << cell.client_concurrency
            << " median_attempt_rps="
            << FormatDouble(cell.aggregate.attempt_requests_per_second.median,
                            2)
            << " median_rps="
            << FormatDouble(cell.aggregate.requests_per_second.median, 2)
            << " median_failure="
            << FormatDouble(cell.aggregate.failure_ratio.median * 100.0, 2)
            << "%"
            << " median_mibps="
            << FormatDouble(cell.aggregate.mib_per_second.median, 2)
            << " median_p95_us="
            << FormatDouble(cell.aggregate.latency_p95_us.median, 2)
            << " cv(rps)="
            << FormatDouble(cell.aggregate.requests_per_second.cv * 100.0, 2)
            << "% loadgen_failures(max)="
            << FormatDouble(cell.aggregate.loadgen_failure_count.max, 0)
            << " stability=" << cell.aggregate.stability << "\n";
}

std::string BuildJson(const EnvironmentInfo& environment, const CliConfig& cli,
                      const RunSettings& run_settings,
                      const std::vector<CellResult>& cells) {
  std::ostringstream out;
  out << "{"
      << "\"environment\":{"
      << "\"timestamp_utc\":\"" << EscapeJson(environment.timestamp_utc)
      << "\","
      << "\"os\":\"" << EscapeJson(environment.os) << "\","
      << "\"compiler\":\"" << EscapeJson(environment.compiler) << "\","
      << "\"build_type\":\"" << EscapeJson(environment.build_type) << "\","
      << "\"logical_cpu_count\":" << environment.logical_cpu_count << "},"
      << "\"run_config\":{"
      << "\"mode\":\"" << EscapeJson(ToString(run_settings.mode)) << "\","
      << "\"scenario\":\"" << EscapeJson(cli.scenario_name) << "\","
      << "\"profile\":\"" << EscapeJson(ToString(cli.profile)) << "\","
      << "\"pressure\":\""
      << EscapeJson(cli.pressure_name.value_or("profile-default")) << "\","
      << "\"server_url\":\"" << EscapeJson(run_settings.server_url) << "\","
      << "\"warmup_ms\":" << run_settings.warmup_ms << ','
      << "\"duration_ms\":" << run_settings.duration_ms << ','
      << "\"repetitions\":" << run_settings.repetitions << ','
      << "\"cooldown_ms\":" << run_settings.cooldown_ms << ','
      << "\"client_processes\":" << run_settings.client_processes << ','
      << "\"request_body_bytes\":" << run_settings.request_body_bytes << ','
      << "\"response_body_bytes\":" << run_settings.response_body_bytes << ','
      << "\"wrk_threads_per_process\":" << run_settings.wrk_threads_per_process
      << ',' << "\"wrk_bin\":\"" << EscapeJson(run_settings.wrk_bin.string())
      << "\"},"
      << "\"cells\":[";
  for (std::size_t i = 0; i < cells.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    out << JsonCell(cells[i]);
  }
  out << "]"
      << "}";
  return out.str();
}

void WriteJsonFile(const std::filesystem::path& path,
                   std::string_view content) {
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }

  std::ofstream out(path);
  if (!out.is_open()) {
    throw std::runtime_error("Failed to open JSON output file: " +
                             path.string());
  }
  out << content;
}

}  // namespace bsrvcore::benchmark
