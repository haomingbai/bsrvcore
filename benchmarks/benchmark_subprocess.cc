#include "benchmark_subprocess.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "benchmark_runner.h"
#include "benchmark_scenarios.h"
#include "benchmark_util.h"

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <thread>
#endif

namespace {

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    throw std::runtime_error("Failed to open benchmark cell result file: " +
                             path.string());
  }

  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

void WriteTextFile(const std::filesystem::path& path,
                   std::string_view content) {
  std::ofstream out(path);
  if (!out.is_open()) {
    throw std::runtime_error("Failed to open benchmark cell result file: " +
                             path.string());
  }
  out << content;
}

std::string SerializeMetrics(
    const bsrvcore::benchmark::RepetitionMetrics& metrics) {
  std::ostringstream out;
  out << "repetition=" << metrics.repetition << "\n"
      << "attempt_count=" << metrics.attempt_count << "\n"
      << "success_count=" << metrics.success_count << "\n"
      << "error_count=" << metrics.error_count << "\n"
      << "non_2xx_3xx_count=" << metrics.non_2xx_3xx_count << "\n"
      << "socket_connect_error_count=" << metrics.socket_connect_error_count
      << "\n"
      << "socket_read_error_count=" << metrics.socket_read_error_count << "\n"
      << "socket_write_error_count=" << metrics.socket_write_error_count
      << "\n"
      << "socket_timeout_error_count=" << metrics.socket_timeout_error_count
      << "\n"
      << "loadgen_failure_count=" << metrics.loadgen_failure_count << "\n"
      << "bytes_sent=" << metrics.bytes_sent << "\n"
      << "bytes_received=" << metrics.bytes_received << "\n"
      << "duration_seconds="
      << bsrvcore::benchmark::FormatDouble(metrics.duration_seconds, 9) << "\n"
      << "attempt_requests_per_second="
      << bsrvcore::benchmark::FormatDouble(metrics.attempt_requests_per_second,
                                           9)
      << "\n"
      << "requests_per_second="
      << bsrvcore::benchmark::FormatDouble(metrics.requests_per_second, 9)
      << "\n"
      << "failure_ratio="
      << bsrvcore::benchmark::FormatDouble(metrics.failure_ratio, 9) << "\n"
      << "mib_per_second="
      << bsrvcore::benchmark::FormatDouble(metrics.mib_per_second, 9) << "\n"
      << "latency_p50_us="
      << bsrvcore::benchmark::FormatDouble(metrics.latency_p50_us, 9) << "\n"
      << "latency_p95_us="
      << bsrvcore::benchmark::FormatDouble(metrics.latency_p95_us, 9) << "\n"
      << "latency_p99_us="
      << bsrvcore::benchmark::FormatDouble(metrics.latency_p99_us, 9) << "\n"
      << "latency_max_us="
      << bsrvcore::benchmark::FormatDouble(metrics.latency_max_us, 9) << "\n";
  return out.str();
}

bsrvcore::benchmark::RepetitionMetrics ParseMetrics(std::string_view text) {
  bsrvcore::benchmark::RepetitionMetrics metrics;
  std::istringstream in{std::string(text)};
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      throw std::runtime_error("Malformed benchmark cell result line: " + line);
    }

    const auto key = line.substr(0, separator);
    const auto value = line.substr(separator + 1);
    if (key == "repetition") {
      metrics.repetition = static_cast<std::size_t>(std::stoull(value));
    } else if (key == "attempt_count") {
      metrics.attempt_count = std::stoull(value);
    } else if (key == "success_count") {
      metrics.success_count = std::stoull(value);
    } else if (key == "error_count") {
      metrics.error_count = std::stoull(value);
    } else if (key == "non_2xx_3xx_count") {
      metrics.non_2xx_3xx_count = std::stoull(value);
    } else if (key == "socket_connect_error_count") {
      metrics.socket_connect_error_count = std::stoull(value);
    } else if (key == "socket_read_error_count") {
      metrics.socket_read_error_count = std::stoull(value);
    } else if (key == "socket_write_error_count") {
      metrics.socket_write_error_count = std::stoull(value);
    } else if (key == "socket_timeout_error_count") {
      metrics.socket_timeout_error_count = std::stoull(value);
    } else if (key == "loadgen_failure_count") {
      metrics.loadgen_failure_count = std::stoull(value);
    } else if (key == "bytes_sent") {
      metrics.bytes_sent = std::stoull(value);
    } else if (key == "bytes_received") {
      metrics.bytes_received = std::stoull(value);
    } else if (key == "duration_seconds") {
      metrics.duration_seconds = std::stod(value);
    } else if (key == "attempt_requests_per_second") {
      metrics.attempt_requests_per_second = std::stod(value);
    } else if (key == "requests_per_second") {
      metrics.requests_per_second = std::stod(value);
    } else if (key == "failure_ratio") {
      metrics.failure_ratio = std::stod(value);
    } else if (key == "mib_per_second") {
      metrics.mib_per_second = std::stod(value);
    } else if (key == "latency_p50_us") {
      metrics.latency_p50_us = std::stod(value);
    } else if (key == "latency_p95_us") {
      metrics.latency_p95_us = std::stod(value);
    } else if (key == "latency_p99_us") {
      metrics.latency_p99_us = std::stod(value);
    } else if (key == "latency_max_us") {
      metrics.latency_max_us = std::stod(value);
    }
  }
  return metrics;
}

std::runtime_error MakeInternalArgumentError(std::string_view message) {
  return std::runtime_error("Invalid internal benchmark invocation: " +
                            std::string(message));
}

}  // namespace

namespace bsrvcore::benchmark {

int RunInternalCell(const CliConfig& cli,
                    const std::vector<ScenarioDefinition>& scenarios) {
  try {
    if (!cli.internal_scenario_name.has_value() ||
        !cli.internal_pressure_name.has_value() ||
        !cli.internal_server_threads.has_value() ||
        !cli.internal_client_concurrency.has_value() ||
        !cli.internal_client_processes.has_value() ||
        !cli.internal_wrk_threads_per_process.has_value() ||
        !cli.internal_wrk_bin.has_value() ||
        !cli.internal_warmup_ms.has_value() ||
        !cli.internal_duration_ms.has_value() ||
        !cli.internal_cooldown_ms.has_value() ||
        !cli.internal_repetition.has_value() ||
        !cli.internal_result_path.has_value()) {
      throw MakeInternalArgumentError("missing one or more required flags");
    }

    PressureSettings pressure;
    pressure.kind = PressureKind::kCustom;
    pressure.name = *cli.internal_pressure_name;
    pressure.server_threads = *cli.internal_server_threads;
    pressure.client_concurrency = *cli.internal_client_concurrency;

    RunSettings run_settings;
    run_settings.warmup_ms = *cli.internal_warmup_ms;
    run_settings.duration_ms = *cli.internal_duration_ms;
    run_settings.cooldown_ms = *cli.internal_cooldown_ms;
    run_settings.repetitions = 1;
    run_settings.client_processes = *cli.internal_client_processes;
    run_settings.wrk_threads_per_process =
        *cli.internal_wrk_threads_per_process;
    run_settings.wrk_bin = *cli.internal_wrk_bin;

    const auto& scenario = FindScenario(scenarios, *cli.internal_scenario_name);
    const auto metrics =
        RunCellRepetition(scenario, pressure, run_settings,
                          *cli.internal_repetition);
    WriteTextFile(*cli.internal_result_path, SerializeMetrics(metrics));
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "benchmark cell failed: " << ex.what() << "\n";
    return 1;
  }
}

#if defined(_WIN32)

RepetitionMetrics RunCellInSubprocess(
    const std::filesystem::path& executable_path,
    const ScenarioDefinition& scenario, const PressureSettings& pressure,
    const RunSettings& run_settings,
    std::size_t repetition) {
  (void)executable_path;
  return RunCellRepetition(scenario, pressure, run_settings, repetition);
}

#else

namespace {

constexpr auto kWaitPollInterval = std::chrono::milliseconds(100);
constexpr std::size_t kCellTimeoutMinSlackMs = 20'000;
constexpr std::size_t kCellTimeoutMaxSlackMs = 120'000;

std::chrono::milliseconds ComputeCellTimeoutSlack(
    const RunSettings& run_settings, const PressureSettings& pressure) {
  const std::size_t phase_ms =
      run_settings.warmup_ms + run_settings.duration_ms + run_settings.cooldown_ms;
  const std::size_t concurrency_budget_ms =
      std::min<std::size_t>(45'000, pressure.client_concurrency * 120);
  const std::size_t process_budget_ms =
      std::min<std::size_t>(20'000, run_settings.client_processes * 800);
  const std::size_t server_budget_ms =
      std::min<std::size_t>(15'000, pressure.server_threads * 200);
  const std::size_t phase_budget_ms =
      std::min<std::size_t>(20'000, phase_ms / 2);

  const std::size_t slack_ms = std::clamp<std::size_t>(
      10'000 + concurrency_budget_ms + process_budget_ms + server_budget_ms +
          phase_budget_ms,
      kCellTimeoutMinSlackMs, kCellTimeoutMaxSlackMs);
  return std::chrono::milliseconds(slack_ms);
}

std::filesystem::path MakeTempResultPath() {
  std::string pattern = (std::filesystem::temp_directory_path() /
                         "bsrvcore-benchmark-cell-XXXXXX.txt")
                            .string();
  std::vector<char> writable(pattern.begin(), pattern.end());
  writable.push_back('\0');

  const int fd = ::mkstemps(writable.data(), 4);
  if (fd == -1) {
    throw std::runtime_error("mkstemps failed: " +
                             std::string(std::strerror(errno)));
  }
  ::close(fd);
  return writable.data();
}

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

std::string TrimErrorMessage(std::string message) {
  message = Trim(std::move(message));
  return message.empty() ? std::string("no stderr output") : message;
}

std::vector<std::string> BuildChildArgs(
    const std::filesystem::path& executable_path,
    const ScenarioDefinition& scenario, const PressureSettings& pressure,
    const RunSettings& run_settings,
    std::size_t repetition, const std::filesystem::path& result_path) {
  return {executable_path.string(),
          "--internal-run-cell",
          "--internal-scenario",
          scenario.name,
          "--internal-pressure-name",
          pressure.name,
          "--internal-server-threads",
          std::to_string(pressure.server_threads),
          "--internal-client-concurrency",
          std::to_string(pressure.client_concurrency),
          "--internal-client-processes",
          std::to_string(run_settings.client_processes),
          "--internal-wrk-threads-per-process",
          std::to_string(run_settings.wrk_threads_per_process),
          "--internal-wrk-bin",
          run_settings.wrk_bin.string(),
          "--internal-warmup-ms",
          std::to_string(run_settings.warmup_ms),
          "--internal-duration-ms",
          std::to_string(run_settings.duration_ms),
          "--internal-cooldown-ms",
          std::to_string(run_settings.cooldown_ms),
          "--internal-repetition",
          std::to_string(repetition),
          "--internal-result-path",
          result_path.string()};
}

}  // namespace

RepetitionMetrics RunCellInSubprocess(
    const std::filesystem::path& executable_path,
    const ScenarioDefinition& scenario, const PressureSettings& pressure,
    const RunSettings& run_settings,
    std::size_t repetition) {
  const auto result_path = MakeTempResultPath();
  const auto cleanup_result = [&]() {
    std::error_code ec;
    std::filesystem::remove(result_path, ec);
  };

  int pipe_fds[2];
  if (::pipe(pipe_fds) != 0) {
    cleanup_result();
    throw std::runtime_error("pipe failed: " +
                             std::string(std::strerror(errno)));
  }

  const pid_t child_pid = ::fork();
  if (child_pid == -1) {
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    cleanup_result();
    throw std::runtime_error("fork failed: " +
                             std::string(std::strerror(errno)));
  }

  if (child_pid == 0) {
    ::dup2(pipe_fds[1], STDERR_FILENO);
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);

    auto args = BuildChildArgs(executable_path, scenario, pressure, run_settings,
                               repetition, result_path);
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    ::execv(argv[0], argv.data());
    std::cerr << "execv failed: " << std::strerror(errno) << "\n";
    _exit(127);
  }

  ::close(pipe_fds[1]);

  const auto phase_duration_ms =
      run_settings.warmup_ms + run_settings.duration_ms + run_settings.cooldown_ms;
  const auto timeout_slack = ComputeCellTimeoutSlack(run_settings, pressure);
  const auto deadline = std::chrono::steady_clock::now() + timeout_slack +
                        std::chrono::milliseconds(phase_duration_ms);

  int status = 0;
  bool timed_out = false;
  while (true) {
    const pid_t wait_result = ::waitpid(child_pid, &status, WNOHANG);
    if (wait_result == child_pid) {
      break;
    }
    if (wait_result == -1) {
      const auto read_error = ReadAllFromFd(pipe_fds[0]);
      ::close(pipe_fds[0]);
      cleanup_result();
      throw std::runtime_error(
          "waitpid failed: " + std::string(std::strerror(errno)) + " [" +
          TrimErrorMessage(read_error) + "]");
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      timed_out = true;
      ::kill(child_pid, SIGKILL);
      ::waitpid(child_pid, &status, 0);
      break;
    }
    std::this_thread::sleep_for(kWaitPollInterval);
  }

  const auto child_stderr = TrimErrorMessage(ReadAllFromFd(pipe_fds[0]));
  ::close(pipe_fds[0]);

  if (timed_out) {
    cleanup_result();
    throw std::runtime_error(
        "benchmark cell timed out for " + scenario.name + "/" + pressure.name +
        " after " + std::to_string(phase_duration_ms + timeout_slack.count()) +
        "ms [" + child_stderr + "]");
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    cleanup_result();
    throw std::runtime_error("benchmark cell subprocess failed for " +
                             scenario.name + "/" + pressure.name + " [" +
                             child_stderr + "]");
  }

  const auto metrics = ParseMetrics(ReadTextFile(result_path));
  cleanup_result();
  return metrics;
}

#endif

}  // namespace bsrvcore::benchmark
