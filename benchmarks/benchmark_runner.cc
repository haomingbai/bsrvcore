#include "benchmark_runner.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http/verb.hpp>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "benchmark_subprocess.h"
#include "benchmark_util.h"
#include "bsrvcore/core/http_server.h"

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

constexpr std::size_t kServerStartRetries = 8;
constexpr auto kServerDrainDelay = std::chrono::milliseconds(20);
constexpr auto kLoadgenPollInterval = std::chrono::milliseconds(100);
constexpr auto kLoadgenTimeoutSlack = std::chrono::milliseconds(15'000);
constexpr auto kServerRunPollInterval = std::chrono::milliseconds(250);

using Clock = std::chrono::steady_clock;

struct CommandResult {
  int exit_code = -1;
  bool timed_out = false;
  std::string output;
};

struct WrkProcessMetrics {
  std::uint64_t requests = 0;
  std::uint64_t non_2xx_3xx = 0;
  std::uint64_t socket_connect_errors = 0;
  std::uint64_t socket_read_errors = 0;
  std::uint64_t socket_write_errors = 0;
  std::uint64_t socket_timeout_errors = 0;
  std::uint64_t bytes_read = 0;
  double duration_seconds = 0.0;
  double latency_p50_us = 0.0;
  double latency_p95_us = 0.0;
  double latency_p99_us = 0.0;
  double latency_max_us = 0.0;
  int exit_code = -1;
  bool timed_out = false;
  std::string diagnostic;
};

bool TraceEnabled() {
  static const bool enabled =
      std::getenv("BSRVCORE_BENCHMARK_TRACE") != nullptr;
  return enabled;
}

void Trace(std::string_view message) {
  if (TraceEnabled()) {
    std::cerr << "[trace] " << message << "\n";
  }
}

unsigned short FindFreePort() {
  asio::io_context ioc;
  tcp::acceptor acceptor(ioc, {asio::ip::make_address("127.0.0.1"), 0});
  return acceptor.local_endpoint().port();
}

class ServerGuard {
 public:
  explicit ServerGuard(bsrvcore::OwnedPtr<bsrvcore::HttpServer> server)
      : server_(std::move(server)) {}

  ~ServerGuard() {
    if (server_) {
      server_->Stop();
    }
  }

 private:
  bsrvcore::OwnedPtr<bsrvcore::HttpServer> server_;
};

struct StartedServer {
  bsrvcore::OwnedPtr<ServerGuard> guard;
  unsigned short port = 0;
};

std::atomic<bool> g_server_stop_requested = false;

extern "C" void HandleServerSignal(int) { g_server_stop_requested.store(true); }

bsrvcore::OwnedPtr<bsrvcore::HttpServer> BuildServer(
    const bsrvcore::benchmark::ScenarioDefinition& scenario,
    const bsrvcore::benchmark::PressureSettings& pressure) {
  bsrvcore::HttpServerExecutorOptions options;
  options.core_thread_num = pressure.server_worker_threads;
  options.max_thread_num = pressure.server_worker_threads;

  auto server = bsrvcore::AllocateUnique<bsrvcore::HttpServer>(options);
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
    const bsrvcore::benchmark::PressureSettings& pressure) {
  std::string last_error = "unknown";
  for (std::size_t attempt = 0; attempt < kServerStartRetries; ++attempt) {
    auto server = BuildServer(scenario, pressure);
    try {
      const auto port = FindFreePort();
      server->AddListen({asio::ip::make_address("127.0.0.1"), port});
      if (!server->Start(pressure.server_io_threads)) {
        last_error = "HttpServer::Start returned false";
        server->Stop();
        continue;
      }
      return {bsrvcore::AllocateUnique<ServerGuard>(std::move(server)), port};
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

StartedServer StartServerAt(
    const bsrvcore::benchmark::ScenarioDefinition& scenario,
    const bsrvcore::benchmark::PressureSettings& pressure,
    std::string_view listen_host, unsigned short port) {
  auto server = BuildServer(scenario, pressure);
  server->AddListen({asio::ip::make_address(std::string(listen_host)), port});
  if (!server->Start(pressure.server_io_threads)) {
    throw std::runtime_error("HttpServer::Start returned false");
  }
  return {bsrvcore::AllocateUnique<ServerGuard>(std::move(server)), port};
}

std::string BuildServerUrl(std::string_view host, unsigned short port) {
  return "http://" + std::string(host) + ":" + std::to_string(port);
}

std::uint64_t ParseUnsigned(std::string token) {
  token.erase(std::remove(token.begin(), token.end(), ','), token.end());
  if (token.empty()) {
    return 0;
  }
  return std::stoull(token);
}

double ParseDouble(std::string_view token) {
  return std::stod(std::string(token));
}

double ParseDurationSeconds(std::string_view token) {
  const std::string value(token);
  if (value.empty()) {
    return 0.0;
  }

  auto parse_suffix = [&](std::string_view suffix, double multiplier) {
    if (value.size() > suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
            0) {
      return ParseDouble(std::string_view(value).substr(
                 0, value.size() - suffix.size())) *
             multiplier;
    }
    return -1.0;
  };

  if (const double us = parse_suffix("us", 1e-6); us >= 0.0) {
    return us;
  }
  if (const double ms = parse_suffix("ms", 1e-3); ms >= 0.0) {
    return ms;
  }
  if (const double sec = parse_suffix("s", 1.0); sec >= 0.0) {
    return sec;
  }
  if (const double min = parse_suffix("m", 60.0); min >= 0.0) {
    return min;
  }
  if (const double hr = parse_suffix("h", 3600.0); hr >= 0.0) {
    return hr;
  }

  return ParseDouble(value);
}

double ParseDurationMicros(std::string_view token) {
  return ParseDurationSeconds(token) * 1'000'000.0;
}

std::uint64_t ParseSizeBytes(std::string_view token) {
  const std::string value(token);
  if (value.empty()) {
    return 0;
  }

  auto parse_unit = [&](std::string_view suffix, double multiplier) {
    if (value.size() > suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
            0) {
      const double count = ParseDouble(
          std::string_view(value).substr(0, value.size() - suffix.size()));
      return static_cast<std::uint64_t>(std::llround(count * multiplier));
    }
    return static_cast<std::uint64_t>(
        std::numeric_limits<std::uint64_t>::max());
  };

  if (const auto gb = parse_unit("GB", 1024.0 * 1024.0 * 1024.0);
      gb != std::numeric_limits<std::uint64_t>::max()) {
    return gb;
  }
  if (const auto mb = parse_unit("MB", 1024.0 * 1024.0);
      mb != std::numeric_limits<std::uint64_t>::max()) {
    return mb;
  }
  if (const auto kb = parse_unit("KB", 1024.0);
      kb != std::numeric_limits<std::uint64_t>::max()) {
    return kb;
  }
  if (const auto b = parse_unit("B", 1.0);
      b != std::numeric_limits<std::uint64_t>::max()) {
    return b;
  }

  return ParseUnsigned(value);
}

std::size_t DurationMsToSeconds(std::size_t duration_ms) {
  return std::max<std::size_t>(1, (duration_ms + 999) / 1000);
}

std::vector<std::size_t> SplitEven(std::size_t total, std::size_t parts) {
  parts = std::max<std::size_t>(1, parts);
  std::vector<std::size_t> split(parts, total / parts);
  for (std::size_t i = 0; i < total % parts; ++i) {
    ++split[i];
  }
  return split;
}

std::string LuaLongString(std::string_view value) {
  std::size_t eq_count = 0;
  while (true) {
    const std::string delimiter = "]" + std::string(eq_count, '=') + "]";
    if (value.find(delimiter) == std::string_view::npos) {
      return "[" + std::string(eq_count, '=') + "[" + std::string(value) + "]" +
             std::string(eq_count, '=') + "]";
    }
    ++eq_count;
  }
}

std::filesystem::path MakeTempLuaScriptPath() {
#if !defined(_WIN32)
  std::string pattern = (std::filesystem::temp_directory_path() /
                         "bsrvcore-benchmark-wrk-XXXXXX.lua")
                            .string();
  std::vector<char> writable(pattern.begin(), pattern.end());
  writable.push_back('\0');
  const int fd = ::mkstemps(writable.data(), 4);
  if (fd == -1) {
    throw std::runtime_error("mkstemps failed while creating wrk script: " +
                             std::string(std::strerror(errno)));
  }
  ::close(fd);
  return std::filesystem::path(writable.data());
#else
  throw std::runtime_error("wrk benchmark scripts are unsupported on Windows");
#endif
}

std::filesystem::path WriteWrkScript(
    const bsrvcore::benchmark::RequestSpec& request) {
  const auto path = MakeTempLuaScriptPath();
  std::ofstream out(path);
  if (!out.is_open()) {
    throw std::runtime_error("Failed to write wrk Lua script: " +
                             path.string());
  }

  out << "wrk.method = "
      << LuaLongString(boost::beast::http::to_string(request.method)) << "\n";
  out << "wrk.path = " << LuaLongString(request.target) << "\n";
  out << "wrk.body = " << LuaLongString(request.body) << "\n";
  for (const auto& [field, value] : request.headers) {
    out << "wrk.headers["
        << LuaLongString(std::string(boost::beast::http::to_string(field)))
        << "] = " << LuaLongString(value) << "\n";
  }
  if (!request.keep_alive) {
    out << "wrk.headers[\"Connection\"] = \"close\"\n";
  }
  return path;
}

std::uint64_t ApproximateRequestBytes(
    const bsrvcore::benchmark::RequestSpec& request) {
  constexpr std::string_view kHttpVersion = "HTTP/1.1";
  std::uint64_t total = boost::beast::http::to_string(request.method).size() +
                        1 + request.target.size() + 1 + kHttpVersion.size() + 2;
  total += 64;  // Host/User-Agent/Accept defaults in wrk.
  for (const auto& [field, value] : request.headers) {
    total += boost::beast::http::to_string(field).size() + 2 + value.size() + 2;
  }
  if (!request.body.empty()) {
    total += std::string_view("Content-Length").size() + 2 +
             std::to_string(request.body.size()).size() + 2;
  }
  total += 2;
  total += request.body.size();
  return total;
}

double Interpolate95(double p90, double p99) {
  return p90 + (p99 - p90) * (5.0 / 9.0);
}

WrkProcessMetrics ParseWrkOutput(std::string_view output) {
  WrkProcessMetrics metrics;
  std::array<double, 100> latency_percentiles{};
  std::array<bool, 100> latency_seen{};

  std::regex requests_line(
      R"(\s*([0-9,]+)\s+requests in\s+([0-9.]+[a-zA-Z]+),\s+([0-9.]+[A-Za-z]+)\s+read)");
  std::regex non_2xx_line(R"(Non-2xx or 3xx responses:\s+([0-9,]+))");
  std::regex socket_errors_line(
      R"(Socket errors:\s+connect\s+([0-9,]+),\s+read\s+([0-9,]+),\s+write\s+([0-9,]+),\s+timeout\s+([0-9,]+))");
  std::regex latency_line(
      R"(^\s*Latency\s+([0-9.]+[a-zA-Z]+)\s+([0-9.]+[a-zA-Z]+)\s+([0-9.]+[a-zA-Z]+).*$)");
  std::regex percentile_line(R"(^\s*([0-9]+)%\s+([0-9.]+[a-zA-Z]+)\s*$)");

  std::smatch matches;
  std::istringstream in{std::string(output)};
  std::string line;
  while (std::getline(in, line)) {
    if (std::regex_search(line, matches, requests_line)) {
      metrics.requests = ParseUnsigned(matches[1].str());
      metrics.duration_seconds = ParseDurationSeconds(matches[2].str());
      metrics.bytes_read = ParseSizeBytes(matches[3].str());
      continue;
    }
    if (std::regex_search(line, matches, non_2xx_line)) {
      metrics.non_2xx_3xx = ParseUnsigned(matches[1].str());
      continue;
    }
    if (std::regex_search(line, matches, socket_errors_line)) {
      metrics.socket_connect_errors = ParseUnsigned(matches[1].str());
      metrics.socket_read_errors = ParseUnsigned(matches[2].str());
      metrics.socket_write_errors = ParseUnsigned(matches[3].str());
      metrics.socket_timeout_errors = ParseUnsigned(matches[4].str());
      continue;
    }
    if (std::regex_search(line, matches, latency_line)) {
      metrics.latency_max_us = ParseDurationMicros(matches[3].str());
      continue;
    }
    if (std::regex_match(line, matches, percentile_line)) {
      const auto percentile =
          static_cast<std::size_t>(std::stoul(matches[1].str()));
      if (percentile < latency_percentiles.size()) {
        latency_percentiles[percentile] = ParseDurationMicros(matches[2].str());
        latency_seen[percentile] = true;
      }
      continue;
    }
  }

  if (latency_seen[50]) {
    metrics.latency_p50_us = latency_percentiles[50];
  }
  if (latency_seen[99]) {
    metrics.latency_p99_us = latency_percentiles[99];
  }
  if (latency_seen[95]) {
    metrics.latency_p95_us = latency_percentiles[95];
  } else if (latency_seen[90] && latency_seen[99]) {
    metrics.latency_p95_us =
        Interpolate95(latency_percentiles[90], latency_percentiles[99]);
  } else if (latency_seen[99]) {
    metrics.latency_p95_us = latency_percentiles[99];
  }

  return metrics;
}

#if !defined(_WIN32)

std::string ReadAllFromFd(int fd) {
  std::string result;
  char buffer[4096];
  while (true) {
    const ssize_t count = ::read(fd, buffer, sizeof(buffer));
    if (count == 0) {
      break;
    }
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("read(pipe) failed: " +
                               std::string(std::strerror(errno)));
    }
    result.append(buffer, static_cast<std::size_t>(count));
  }
  return result;
}

CommandResult RunCommandCapture(const std::vector<std::string>& args,
                                std::chrono::milliseconds timeout) {
  if (args.empty()) {
    throw std::invalid_argument("RunCommandCapture called with empty args");
  }

  int pipe_fds[2];
  if (::pipe(pipe_fds) != 0) {
    throw std::runtime_error("pipe failed: " +
                             std::string(std::strerror(errno)));
  }

  const pid_t child_pid = ::fork();
  if (child_pid == -1) {
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    throw std::runtime_error("fork failed: " +
                             std::string(std::strerror(errno)));
  }

  if (child_pid == 0) {
    ::dup2(pipe_fds[1], STDOUT_FILENO);
    ::dup2(pipe_fds[1], STDERR_FILENO);
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    ::execvp(argv[0], argv.data());
    std::cerr << "execvp failed: " << std::strerror(errno) << "\n";
    _exit(127);
  }

  ::close(pipe_fds[1]);
  int status = 0;
  bool timed_out = false;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true) {
    const pid_t wait_result = ::waitpid(child_pid, &status, WNOHANG);
    if (wait_result == child_pid) {
      break;
    }
    if (wait_result == -1) {
      const auto output = ReadAllFromFd(pipe_fds[0]);
      ::close(pipe_fds[0]);
      throw std::runtime_error(
          "waitpid failed: " + std::string(std::strerror(errno)) + " output=[" +
          bsrvcore::benchmark::Trim(output) + "]");
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      timed_out = true;
      ::kill(child_pid, SIGKILL);
      ::waitpid(child_pid, &status, 0);
      break;
    }
    std::this_thread::sleep_for(kLoadgenPollInterval);
  }

  CommandResult result;
  result.timed_out = timed_out;
  result.output = ReadAllFromFd(pipe_fds[0]);
  ::close(pipe_fds[0]);

  if (!WIFEXITED(status)) {
    result.exit_code = timed_out ? 124 : -1;
  } else {
    result.exit_code = WEXITSTATUS(status);
  }
  return result;
}

#else

CommandResult RunCommandCapture(const std::vector<std::string>&,
                                std::chrono::milliseconds) {
  throw std::runtime_error("wrk benchmark runner is unsupported on Windows");
}

#endif

WrkProcessMetrics RunWrkProcess(const std::filesystem::path& wrk_bin,
                                const std::filesystem::path& wrk_script,
                                std::size_t connections, std::size_t threads,
                                std::size_t duration_seconds,
                                std::string_view server_url) {
  std::vector<std::string> args = {wrk_bin.string(),
                                   "--latency",
                                   "-t",
                                   std::to_string(threads),
                                   "-c",
                                   std::to_string(connections),
                                   "-d",
                                   std::to_string(duration_seconds) + "s",
                                   "-s",
                                   wrk_script.string(),
                                   std::string(server_url)};

  const auto timeout =
      std::chrono::milliseconds(duration_seconds * 1000) + kLoadgenTimeoutSlack;
  const auto command = RunCommandCapture(args, timeout);

  auto parsed = ParseWrkOutput(command.output);
  parsed.exit_code = command.exit_code;
  parsed.timed_out = command.timed_out;
  if (command.timed_out) {
    parsed.diagnostic = "wrk timed out";
  } else if (command.exit_code != 0) {
    parsed.diagnostic =
        "wrk exited with code " + std::to_string(command.exit_code);
  }
  if (!bsrvcore::benchmark::Trim(command.output).empty()) {
    if (!parsed.diagnostic.empty()) {
      parsed.diagnostic += " ";
    }
    parsed.diagnostic += "[" + bsrvcore::benchmark::Trim(command.output) + "]";
  }
  return parsed;
}

std::vector<WrkProcessMetrics> RunWrkPhase(
    const bsrvcore::benchmark::PressureSettings& pressure,
    const bsrvcore::benchmark::RunSettings& run_settings,
    const std::filesystem::path& wrk_script, std::string_view server_url,
    std::size_t duration_ms) {
  const std::size_t process_count = std::max<std::size_t>(
      1, std::min(run_settings.client_processes, pressure.client_concurrency));
  const std::size_t duration_seconds = DurationMsToSeconds(duration_ms);
  auto split = SplitEven(pressure.client_concurrency, process_count);

  std::vector<WrkProcessMetrics> process_results(process_count);
  std::vector<std::jthread> workers;
  workers.reserve(process_count);
  for (std::size_t i = 0; i < process_count; ++i) {
    const std::size_t connections = std::max<std::size_t>(1, split[i]);
    const std::size_t threads = std::max<std::size_t>(
        1, std::min(run_settings.wrk_threads_per_process, connections));
    workers.emplace_back([&, i, connections, threads] {
      try {
        process_results[i] =
            RunWrkProcess(run_settings.wrk_bin, wrk_script, connections,
                          threads, duration_seconds, server_url);
      } catch (const std::exception& ex) {
        process_results[i].exit_code = -1;
        process_results[i].diagnostic = ex.what();
      }
    });
  }
  workers.clear();
  return process_results;
}

bsrvcore::benchmark::RepetitionMetrics BuildMeasuredMetrics(
    std::size_t repetition, std::size_t duration_ms,
    const bsrvcore::benchmark::RequestSpec& request,
    const std::vector<WrkProcessMetrics>& process_runs) {
  bsrvcore::benchmark::RepetitionMetrics metrics;
  metrics.repetition = repetition;

  double duration_seconds =
      static_cast<double>(DurationMsToSeconds(duration_ms));
  for (const auto& run : process_runs) {
    duration_seconds = std::max(duration_seconds, run.duration_seconds);
  }
  metrics.duration_seconds = duration_seconds;

  const std::uint64_t request_bytes = ApproximateRequestBytes(request);
  double weighted_p50 = 0.0;
  double weighted_p95 = 0.0;
  double weighted_p99 = 0.0;
  double weighted_total = 0.0;

  for (const auto& run : process_runs) {
    if (!run.diagnostic.empty()) {
      Trace(std::string("wrk diagnostic: ") + run.diagnostic);
    }
    const std::uint64_t socket_errors =
        run.socket_connect_errors + run.socket_read_errors +
        run.socket_write_errors + run.socket_timeout_errors;
    const std::uint64_t success_count =
        run.requests > run.non_2xx_3xx ? run.requests - run.non_2xx_3xx : 0;
    const std::uint64_t error_count = run.non_2xx_3xx + socket_errors;

    metrics.success_count += success_count;
    metrics.error_count += error_count;
    metrics.non_2xx_3xx_count += run.non_2xx_3xx;
    metrics.socket_connect_error_count += run.socket_connect_errors;
    metrics.socket_read_error_count += run.socket_read_errors;
    metrics.socket_write_error_count += run.socket_write_errors;
    metrics.socket_timeout_error_count += run.socket_timeout_errors;
    metrics.bytes_received += run.bytes_read;
    metrics.bytes_sent += request_bytes * (success_count + error_count);
    if (run.exit_code != 0 || run.timed_out) {
      ++metrics.loadgen_failure_count;
    }

    if (success_count > 0) {
      const double weight = static_cast<double>(success_count);
      weighted_p50 += run.latency_p50_us * weight;
      weighted_p95 += run.latency_p95_us * weight;
      weighted_p99 += run.latency_p99_us * weight;
      weighted_total += weight;
      metrics.latency_max_us =
          std::max(metrics.latency_max_us, run.latency_max_us);
    }
  }

  metrics.attempt_count = metrics.success_count + metrics.error_count;
  if (weighted_total > 0.0) {
    metrics.latency_p50_us = weighted_p50 / weighted_total;
    metrics.latency_p95_us = weighted_p95 / weighted_total;
    metrics.latency_p99_us = weighted_p99 / weighted_total;
  }

  if (metrics.duration_seconds > 0.0) {
    metrics.attempt_requests_per_second =
        static_cast<double>(metrics.attempt_count) / metrics.duration_seconds;
    metrics.requests_per_second =
        static_cast<double>(metrics.success_count) / metrics.duration_seconds;
    metrics.mib_per_second =
        static_cast<double>(metrics.bytes_sent + metrics.bytes_received) /
        (1024.0 * 1024.0) / metrics.duration_seconds;
  }
  if (metrics.attempt_count > 0) {
    metrics.failure_ratio = static_cast<double>(metrics.error_count) /
                            static_cast<double>(metrics.attempt_count);
  }
  return metrics;
}

}  // namespace

namespace bsrvcore::benchmark {

RepetitionMetrics RunCellRepetition(const ScenarioDefinition& scenario,
                                    const PressureSettings& pressure,
                                    const RunSettings& run_settings,
                                    std::size_t repetition) {
  Trace(std::string("start cell ") + scenario.name + "/" + pressure.name);
  const bool use_remote_server = run_settings.mode == RunMode::kClient;
  StartedServer started_server;
  std::string server_url;
  if (use_remote_server) {
    server_url = run_settings.server_url;
  } else {
    started_server = StartServerWithRetry(scenario, pressure);
    server_url = BuildServerUrl("127.0.0.1", started_server.port);
    Trace(std::string("server started ") + scenario.name + "/" + pressure.name);
  }

  WorkerState request_state;
  const auto request = scenario.make_request(request_state);
  const auto wrk_script = WriteWrkScript(request);
  const auto cleanup_script = [&]() {
    std::error_code ec;
    std::filesystem::remove(wrk_script, ec);
  };

  RepetitionMetrics metrics;
  try {
    if (run_settings.warmup_ms > 0) {
      Trace(std::string("warmup start ") + scenario.name + "/" + pressure.name);
      (void)RunWrkPhase(pressure, run_settings, wrk_script, server_url,
                        run_settings.warmup_ms);
      Trace(std::string("warmup done ") + scenario.name + "/" + pressure.name);
    }

    Trace(std::string("measure start ") + scenario.name + "/" + pressure.name);
    auto measured = RunWrkPhase(pressure, run_settings, wrk_script, server_url,
                                run_settings.duration_ms);
    Trace(std::string("measure done ") + scenario.name + "/" + pressure.name);
    metrics = BuildMeasuredMetrics(repetition, run_settings.duration_ms,
                                   request, measured);

    if (run_settings.cooldown_ms > 0) {
      Trace(std::string("cooldown start ") + scenario.name + "/" +
            pressure.name);
      (void)RunWrkPhase(pressure, run_settings, wrk_script, server_url,
                        run_settings.cooldown_ms);
      Trace(std::string("cooldown done ") + scenario.name + "/" +
            pressure.name);
    }
  } catch (...) {
    cleanup_script();
    started_server.guard.reset();
    throw;
  }

  cleanup_script();
  if (!use_remote_server) {
    std::this_thread::sleep_for(kServerDrainDelay);
    started_server.guard.reset();
  }
  Trace(std::string("cell complete ") + scenario.name + "/" + pressure.name);
  return metrics;
}

int RunServer(const ScenarioDefinition& scenario,
              const PressureSettings& pressure, std::string_view listen_host,
              unsigned short listen_port) {
  if (listen_port == 0) {
    throw std::invalid_argument("server mode requires a non-zero listen port");
  }

  auto started_server =
      StartServerAt(scenario, pressure, listen_host, listen_port);

  g_server_stop_requested.store(false);
  const auto previous_sigint = std::signal(SIGINT, HandleServerSignal);
  const auto previous_sigterm = std::signal(SIGTERM, HandleServerSignal);

  std::cout << "SERVER_READY=1\n";
  std::cout << "SERVER_SCENARIO=" << scenario.name << "\n";
  std::cout << "SERVER_LISTEN_HOST=" << listen_host << "\n";
  std::cout << "SERVER_LISTEN_PORT=" << listen_port << "\n";
  std::cout.flush();

  while (!g_server_stop_requested.load()) {
    std::this_thread::sleep_for(kServerRunPollInterval);
  }

  std::signal(SIGINT, previous_sigint);
  std::signal(SIGTERM, previous_sigterm);
  started_server.guard.reset();
  return 0;
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
      std::rotate(ordered.begin(),
                  ordered.begin() + repetition % ordered.size(), ordered.end());
    }

    for (const ScenarioDefinition* scenario : ordered) {
      for (const auto& pressure : run_settings.pressures) {
        std::cout << "Running " << scenario->name << " under " << pressure.name
                  << " (rep " << (repetition + 1) << "/"
                  << run_settings.repetitions
                  << ", server_io_threads=" << pressure.server_io_threads
                  << ", server_worker_threads="
                  << pressure.server_worker_threads
                  << ", client_concurrency=" << pressure.client_concurrency
                  << ", mode=" << ToString(run_settings.mode)
                  << ", client_processes=" << run_settings.client_processes
                  << ", wrk_threads_per_process="
                  << run_settings.wrk_threads_per_process << ")\n";

        auto metrics = RunCellInSubprocess(executable_path, *scenario, pressure,
                                           run_settings, repetition + 1);
        std::cout << "Finished " << scenario->name << " under " << pressure.name
                  << " (rep " << (repetition + 1) << "/"
                  << run_settings.repetitions << ")\n";

        const auto key = CellKey(scenario->name, pressure.name);
        auto [it, inserted] =
            cells_by_key.emplace(key, CellResult{scenario->name,
                                                 pressure.name,
                                                 pressure.server_io_threads,
                                                 pressure.server_worker_threads,
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
