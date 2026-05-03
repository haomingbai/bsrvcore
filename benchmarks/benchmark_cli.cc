#include "benchmark_cli.h"

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "benchmark_util.h"

namespace po = boost::program_options;

namespace bsrvcore::benchmark {

CliConfig ParseCli(int argc, char** argv, std::string& help_text) {
  CliConfig cli;

  po::options_description options("bsrvcore_http_benchmark options");
  options.add_options()("help,h", "Show help")(
      "list-scenarios", po::bool_switch(&cli.list_scenarios),
      "List available scenarios")(
      "mode", po::value<std::string>()->default_value("local"),
      "Mode: local|client|server")(
      "scenario",
      po::value<std::string>(&cli.scenario_name)->default_value("all"),
      "Scenario name, 'mainline', 'io', or 'all'")(
      "profile", po::value<std::string>()->default_value("quick"),
      "Profile: quick or full")(
      "pressure", po::value<std::string>(),
      "Pressure: light|balanced|saturated|overload|all")(
      "pressure-label", po::value<std::string>(),
      "Custom pressure label used when explicit thread/concurrency overrides "
      "are passed")("server-url", po::value<std::string>(),
                    "Remote benchmark base URL for client mode, for example "
                    "http://host:18080")(
      "listen-host", po::value<std::string>()->default_value("127.0.0.1"),
      "Listen host for server mode")("listen-port", po::value<unsigned short>(),
                                     "Listen port for server mode")(
      "server-threads", po::value<std::size_t>(), "Override server threads")(
      "server-io-threads", po::value<std::size_t>(),
      "Override server io thread count")("server-worker-threads",
                                         po::value<std::size_t>(),
                                         "Override server worker thread count")(
      "client-concurrency", po::value<std::size_t>(),
      "Override total client connections (all client processes)")(
      "client-processes", po::value<std::size_t>(),
      "Number of wrk processes that concurrently pressure one server")(
      "wrk-threads-per-process", po::value<std::size_t>(),
      "wrk threads used by each client process")(
      "request-body-bytes", po::value<std::size_t>(),
      "Override request body size in bytes for parameterized body scenarios")(
      "response-body-bytes", po::value<std::size_t>(),
      "Override response body size in bytes for parameterized body scenarios")(
      "wrk-bin", po::value<std::string>(),
      "Path to wrk binary (default probe: /bin,/usr/bin,/usr/local/bin, then "
      "PATH)")("warmup-ms", po::value<std::size_t>(), "Warmup duration in ms")(
      "duration-ms", po::value<std::size_t>(), "Measure duration in ms")(
      "repetitions", po::value<std::size_t>(), "Number of repetitions")(
      "cooldown-ms", po::value<std::size_t>(), "Cooldown duration in ms")(
      "output-json", po::value<std::string>(), "Write JSON output to path");

  po::options_description hidden("Internal benchmark options");
  hidden.add_options()("internal-run-cell",
                       po::bool_switch(&cli.internal_run_cell),
                       "Run one benchmark cell in internal mode")(
      "internal-mode", po::value<std::string>(),
      "Run mode for internal cell mode")(
      "internal-scenario", po::value<std::string>(),
      "Scenario name for internal cell mode")(
      "internal-pressure-name", po::value<std::string>(),
      "Pressure name for internal cell mode")(
      "internal-server-url", po::value<std::string>(),
      "Remote base URL for internal client cell mode")(
      "internal-server-threads", po::value<std::size_t>(),
      "Server threads for internal cell mode")(
      "internal-server-io-threads", po::value<std::size_t>(),
      "Server io threads for internal cell mode")(
      "internal-server-worker-threads", po::value<std::size_t>(),
      "Server worker threads for internal cell mode")(
      "internal-client-concurrency", po::value<std::size_t>(),
      "Client concurrency for internal cell mode")(
      "internal-client-processes", po::value<std::size_t>(),
      "wrk client process count for internal cell mode")(
      "internal-wrk-threads-per-process", po::value<std::size_t>(),
      "wrk threads per process for internal cell mode")(
      "internal-request-body-bytes", po::value<std::size_t>(),
      "Request body size override for internal cell mode")(
      "internal-response-body-bytes", po::value<std::size_t>(),
      "Response body size override for internal cell mode")(
      "internal-wrk-bin", po::value<std::string>(),
      "wrk binary for internal cell mode")(
      "internal-warmup-ms", po::value<std::size_t>(),
      "Warmup duration for internal cell mode")(
      "internal-duration-ms", po::value<std::size_t>(),
      "Measure duration for internal cell mode")(
      "internal-cooldown-ms", po::value<std::size_t>(),
      "Cooldown duration for internal cell mode")(
      "internal-repetition", po::value<std::size_t>(),
      "Repetition number for internal cell mode")(
      "internal-result-path", po::value<std::string>(),
      "Result path for internal cell mode");

  po::options_description all_options;
  all_options.add(options).add(hidden);

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, all_options), vm);
  po::notify(vm);

  std::ostringstream out;
  out << options;
  help_text = out.str();

  cli.show_help = vm.count("help") != 0;
  if (cli.show_help) {
    return cli;
  }

  cli.mode = ParseRunMode(vm["mode"].as<std::string>());
  const std::string profile = vm["profile"].as<std::string>();
  if (profile == "quick") {
    cli.profile = ProfileKind::kQuick;
  } else if (profile == "full") {
    cli.profile = ProfileKind::kFull;
  } else {
    throw std::invalid_argument("Unknown profile: " + profile);
  }

  if (vm.count("pressure") != 0) {
    cli.pressure_name = vm["pressure"].as<std::string>();
  }
  if (vm.count("pressure-label") != 0) {
    cli.pressure_label = vm["pressure-label"].as<std::string>();
  }
  if (vm.count("server-url") != 0) {
    cli.server_url = vm["server-url"].as<std::string>();
  }
  cli.listen_host = vm["listen-host"].as<std::string>();
  if (vm.count("listen-port") != 0) {
    cli.listen_port = vm["listen-port"].as<unsigned short>();
  }
  if (vm.count("server-threads") != 0) {
    cli.server_threads_override = vm["server-threads"].as<std::size_t>();
  }
  if (vm.count("server-io-threads") != 0) {
    cli.server_io_threads_override = vm["server-io-threads"].as<std::size_t>();
  }
  if (vm.count("server-worker-threads") != 0) {
    cli.server_worker_threads_override =
        vm["server-worker-threads"].as<std::size_t>();
  }
  if (vm.count("client-concurrency") != 0) {
    cli.client_concurrency_override =
        vm["client-concurrency"].as<std::size_t>();
  }
  if (vm.count("client-processes") != 0) {
    cli.client_processes_override = vm["client-processes"].as<std::size_t>();
  }
  if (vm.count("wrk-threads-per-process") != 0) {
    cli.wrk_threads_per_process_override =
        vm["wrk-threads-per-process"].as<std::size_t>();
  }
  if (vm.count("request-body-bytes") != 0) {
    cli.request_body_bytes_override =
        vm["request-body-bytes"].as<std::size_t>();
  }
  if (vm.count("response-body-bytes") != 0) {
    cli.response_body_bytes_override =
        vm["response-body-bytes"].as<std::size_t>();
  }
  if (vm.count("wrk-bin") != 0) {
    cli.wrk_bin_override =
        std::filesystem::path(vm["wrk-bin"].as<std::string>());
  }
  if (vm.count("warmup-ms") != 0) {
    cli.warmup_ms_override = vm["warmup-ms"].as<std::size_t>();
  }
  if (vm.count("duration-ms") != 0) {
    cli.duration_ms_override = vm["duration-ms"].as<std::size_t>();
  }
  if (vm.count("repetitions") != 0) {
    cli.repetitions_override = vm["repetitions"].as<std::size_t>();
  }
  if (vm.count("cooldown-ms") != 0) {
    cli.cooldown_ms_override = vm["cooldown-ms"].as<std::size_t>();
  }
  if (vm.count("output-json") != 0) {
    cli.output_json =
        std::filesystem::path(vm["output-json"].as<std::string>());
  }
  if (vm.count("internal-scenario") != 0) {
    cli.internal_scenario_name = vm["internal-scenario"].as<std::string>();
  }
  if (vm.count("internal-mode") != 0) {
    cli.internal_mode = ParseRunMode(vm["internal-mode"].as<std::string>());
  }
  if (vm.count("internal-pressure-name") != 0) {
    cli.internal_pressure_name = vm["internal-pressure-name"].as<std::string>();
  }
  if (vm.count("internal-server-url") != 0) {
    cli.internal_server_url = vm["internal-server-url"].as<std::string>();
  }
  if (vm.count("internal-server-threads") != 0) {
    cli.internal_server_threads =
        vm["internal-server-threads"].as<std::size_t>();
  }
  if (vm.count("internal-server-io-threads") != 0) {
    cli.internal_server_io_threads =
        vm["internal-server-io-threads"].as<std::size_t>();
  }
  if (vm.count("internal-server-worker-threads") != 0) {
    cli.internal_server_worker_threads =
        vm["internal-server-worker-threads"].as<std::size_t>();
  }
  if (vm.count("internal-client-concurrency") != 0) {
    cli.internal_client_concurrency =
        vm["internal-client-concurrency"].as<std::size_t>();
  }
  if (vm.count("internal-client-processes") != 0) {
    cli.internal_client_processes =
        vm["internal-client-processes"].as<std::size_t>();
  }
  if (vm.count("internal-wrk-threads-per-process") != 0) {
    cli.internal_wrk_threads_per_process =
        vm["internal-wrk-threads-per-process"].as<std::size_t>();
  }
  if (vm.count("internal-request-body-bytes") != 0) {
    cli.internal_request_body_bytes =
        vm["internal-request-body-bytes"].as<std::size_t>();
  }
  if (vm.count("internal-response-body-bytes") != 0) {
    cli.internal_response_body_bytes =
        vm["internal-response-body-bytes"].as<std::size_t>();
  }
  if (vm.count("internal-wrk-bin") != 0) {
    cli.internal_wrk_bin =
        std::filesystem::path(vm["internal-wrk-bin"].as<std::string>());
  }
  if (vm.count("internal-warmup-ms") != 0) {
    cli.internal_warmup_ms = vm["internal-warmup-ms"].as<std::size_t>();
  }
  if (vm.count("internal-duration-ms") != 0) {
    cli.internal_duration_ms = vm["internal-duration-ms"].as<std::size_t>();
  }
  if (vm.count("internal-cooldown-ms") != 0) {
    cli.internal_cooldown_ms = vm["internal-cooldown-ms"].as<std::size_t>();
  }
  if (vm.count("internal-repetition") != 0) {
    cli.internal_repetition = vm["internal-repetition"].as<std::size_t>();
  }
  if (vm.count("internal-result-path") != 0) {
    cli.internal_result_path =
        std::filesystem::path(vm["internal-result-path"].as<std::string>());
  }

  return cli;
}

}  // namespace bsrvcore::benchmark
