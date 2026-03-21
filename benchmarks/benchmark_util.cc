#include "benchmark_util.h"

#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef BSRVCORE_BENCHMARK_COMPILER_ID
#define BSRVCORE_BENCHMARK_COMPILER_ID "unknown"
#endif

#ifndef BSRVCORE_BENCHMARK_COMPILER_VERSION
#define BSRVCORE_BENCHMARK_COMPILER_VERSION "unknown"
#endif

#ifndef BSRVCORE_BENCHMARK_BUILD_TYPE
#define BSRVCORE_BENCHMARK_BUILD_TYPE "unknown"
#endif

namespace bsrvcore::benchmark {

namespace {

std::size_t SafeCpuCount() {
  return std::max<std::size_t>(1, std::thread::hardware_concurrency());
}

PressureSettings ResolvePressure(PressureKind kind, std::size_t cpu_count) {
  PressureSettings pressure;
  pressure.kind = kind;
  pressure.name = ToString(kind);

  switch (kind) {
    case PressureKind::kLight:
      pressure.server_threads = 1;
      pressure.client_concurrency = 1;
      break;
    case PressureKind::kBalanced:
      pressure.server_threads = std::max<std::size_t>(1, (cpu_count + 1) / 2);
      pressure.client_concurrency =
          std::max<std::size_t>(4, pressure.server_threads * 4);
      break;
    case PressureKind::kSaturated:
      pressure.server_threads = std::max<std::size_t>(1, cpu_count);
      pressure.client_concurrency =
          std::max<std::size_t>(16, pressure.server_threads * 8);
      break;
    case PressureKind::kOverload:
      pressure.server_threads = std::max<std::size_t>(1, cpu_count);
      pressure.client_concurrency =
          std::max<std::size_t>(32, pressure.server_threads * 16);
      break;
    case PressureKind::kCustom:
      pressure.server_threads = std::max<std::size_t>(1, cpu_count);
      pressure.client_concurrency =
          std::max<std::size_t>(16, pressure.server_threads * 8);
      pressure.name = "custom";
      break;
  }

  return pressure;
}

ProfileSettings ResolveProfile(ProfileKind profile) {
  if (profile == ProfileKind::kQuick) {
    return {
        {PressureKind::kLight, PressureKind::kSaturated}, 1000, 3000, 2, 500};
  }
  return {{PressureKind::kLight, PressureKind::kBalanced,
           PressureKind::kSaturated, PressureKind::kOverload},
          2000,
          8000,
          5,
          1000};
}

std::string FormatUtcTimestamp(std::chrono::system_clock::time_point tp) {
  auto time = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

}  // namespace

std::string ToString(ProfileKind profile) {
  return profile == ProfileKind::kQuick ? "quick" : "full";
}

std::string ToString(PressureKind kind) {
  switch (kind) {
    case PressureKind::kLight:
      return "light";
    case PressureKind::kBalanced:
      return "balanced";
    case PressureKind::kSaturated:
      return "saturated";
    case PressureKind::kOverload:
      return "overload";
    case PressureKind::kCustom:
      return "custom";
  }
  return "custom";
}

PressureKind ParsePressureKind(const std::string& value) {
  if (value == "light") {
    return PressureKind::kLight;
  }
  if (value == "balanced") {
    return PressureKind::kBalanced;
  }
  if (value == "saturated") {
    return PressureKind::kSaturated;
  }
  if (value == "overload") {
    return PressureKind::kOverload;
  }
  throw std::invalid_argument("Unknown pressure: " + value);
}

RunSettings ResolveRunSettings(const CliConfig& cli) {
  const std::size_t cpu_count = SafeCpuCount();
  const ProfileSettings profile = ResolveProfile(cli.profile);

  RunSettings run;
  run.warmup_ms = cli.warmup_ms_override.value_or(profile.warmup_ms);
  run.duration_ms = cli.duration_ms_override.value_or(profile.duration_ms);
  run.repetitions = cli.repetitions_override.value_or(profile.repetitions);
  run.cooldown_ms = cli.cooldown_ms_override.value_or(profile.cooldown_ms);

  if (run.warmup_ms == 0 || run.duration_ms == 0 || run.repetitions == 0) {
    throw std::invalid_argument("warmup/duration/repetitions must be positive");
  }

  std::vector<PressureKind> pressure_kinds;
  if (!cli.pressure_name.has_value()) {
    pressure_kinds = profile.pressures;
  } else if (*cli.pressure_name == "all") {
    pressure_kinds = {PressureKind::kLight, PressureKind::kBalanced,
                      PressureKind::kSaturated, PressureKind::kOverload};
  } else {
    pressure_kinds = {ParsePressureKind(*cli.pressure_name)};
  }

  if (cli.server_threads_override.has_value() ||
      cli.client_concurrency_override.has_value()) {
    PressureKind base_kind = PressureKind::kSaturated;
    if (pressure_kinds.size() == 1) {
      base_kind = pressure_kinds.front();
    }

    auto pressure = ResolvePressure(base_kind, cpu_count);
    pressure.kind = PressureKind::kCustom;
    pressure.name = "custom";
    if (cli.server_threads_override.has_value()) {
      pressure.server_threads = *cli.server_threads_override;
    }
    if (cli.client_concurrency_override.has_value()) {
      pressure.client_concurrency = *cli.client_concurrency_override;
    }
    if (pressure.server_threads == 0 || pressure.client_concurrency == 0) {
      throw std::invalid_argument(
          "server-threads and client-concurrency must be positive");
    }
    run.pressures.push_back(std::move(pressure));
    return run;
  }

  std::set<std::pair<std::size_t, std::size_t>> seen;
  for (PressureKind kind : pressure_kinds) {
    auto pressure = ResolvePressure(kind, cpu_count);
    const auto key =
        std::make_pair(pressure.server_threads, pressure.client_concurrency);
    if (seen.insert(key).second) {
      run.pressures.push_back(std::move(pressure));
    }
  }

  return run;
}

EnvironmentInfo DetectEnvironment() {
  EnvironmentInfo info;
  info.timestamp_utc = FormatUtcTimestamp(std::chrono::system_clock::now());
  info.compiler = std::string(BSRVCORE_BENCHMARK_COMPILER_ID) + " " +
                  std::string(BSRVCORE_BENCHMARK_COMPILER_VERSION);
  info.build_type = BSRVCORE_BENCHMARK_BUILD_TYPE;
  info.logical_cpu_count = SafeCpuCount();

  struct utsname uts{};
  if (uname(&uts) == 0) {
    info.os = std::string(uts.sysname) + " " + uts.release + " " + uts.machine;
  } else {
    info.os = "unknown";
  }

  return info;
}

std::string EscapeJson(std::string_view input) {
  std::ostringstream out;
  for (unsigned char ch : input) {
    switch (ch) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(ch) << std::dec << std::setfill(' ');
        } else {
          out << static_cast<char>(ch);
        }
        break;
    }
  }
  return out.str();
}

std::string FormatDouble(double value, int precision) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

std::string Trim(std::string value) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
              value.end());
  return value;
}

std::string BuildCookieHeader(
    const std::map<std::string, std::string>& cookies) {
  std::ostringstream out;
  bool first = true;
  for (const auto& [name, value] : cookies) {
    if (!first) {
      out << "; ";
    }
    out << name << "=" << value;
    first = false;
  }
  return out.str();
}

void SyncCookieJar(const boost::beast::http::fields& headers,
                   std::map<std::string, std::string>& cookie_jar) {
  const auto range = headers.equal_range(boost::beast::http::field::set_cookie);
  for (auto it = range.first; it != range.second; ++it) {
    std::string value(it->value());
    const auto semicolon = value.find(';');
    if (semicolon != std::string::npos) {
      value.resize(semicolon);
    }
    const auto equals = value.find('=');
    if (equals == std::string::npos || equals == 0) {
      continue;
    }

    std::string name = Trim(value.substr(0, equals));
    std::string cookie_value = Trim(value.substr(equals + 1));
    if (!name.empty()) {
      cookie_jar[name] = cookie_value;
    }
  }
}

double PercentileFromSorted(const std::vector<std::uint32_t>& sorted,
                            double fraction) {
  if (sorted.empty()) {
    return 0.0;
  }

  const double position = fraction * static_cast<double>(sorted.size() - 1);
  const auto low = static_cast<std::size_t>(std::floor(position));
  const auto high = static_cast<std::size_t>(std::ceil(position));
  if (low == high) {
    return static_cast<double>(sorted[low]);
  }
  const double weight = position - static_cast<double>(low);
  return static_cast<double>(sorted[low]) * (1.0 - weight) +
         static_cast<double>(sorted[high]) * weight;
}

std::vector<std::uint32_t> SampleLatencies(
    const std::vector<std::uint32_t>& latencies) {
  if (latencies.size() <= kMaxLatencySamples) {
    return latencies;
  }

  const std::size_t step =
      (latencies.size() + kMaxLatencySamples - 1) / kMaxLatencySamples;
  std::vector<std::uint32_t> sampled;
  sampled.reserve((latencies.size() + step - 1) / step);
  for (std::size_t i = 0; i < latencies.size(); i += step) {
    sampled.push_back(latencies[i]);
  }
  return sampled;
}

ScalarSummary SummarizeScalar(const std::vector<double>& values) {
  ScalarSummary summary;
  if (values.empty()) {
    return summary;
  }

  std::vector<double> sorted = values;
  std::sort(sorted.begin(), sorted.end());

  summary.min = sorted.front();
  summary.max = sorted.back();
  summary.median =
      sorted.size() % 2 == 0
          ? (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) / 2.0
          : sorted[sorted.size() / 2];
  summary.mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) /
                 static_cast<double>(sorted.size());

  if (sorted.size() > 1) {
    double variance = 0.0;
    for (double value : sorted) {
      const double diff = value - summary.mean;
      variance += diff * diff;
    }
    variance /= static_cast<double>(sorted.size());
    summary.stdev = std::sqrt(variance);
  }

  if (summary.mean != 0.0) {
    summary.cv = summary.stdev / summary.mean;
  }

  return summary;
}

AggregateMetrics AggregateRuns(const std::vector<RepetitionMetrics>& runs) {
  AggregateMetrics aggregate;
  std::vector<double> success_counts;
  std::vector<double> error_counts;
  std::vector<double> bytes_sent;
  std::vector<double> bytes_received;
  std::vector<double> rps;
  std::vector<double> mibps;
  std::vector<double> p50;
  std::vector<double> p95;
  std::vector<double> p99;
  std::vector<double> max;

  success_counts.reserve(runs.size());
  error_counts.reserve(runs.size());
  bytes_sent.reserve(runs.size());
  bytes_received.reserve(runs.size());
  rps.reserve(runs.size());
  mibps.reserve(runs.size());
  p50.reserve(runs.size());
  p95.reserve(runs.size());
  p99.reserve(runs.size());
  max.reserve(runs.size());

  for (const auto& run : runs) {
    success_counts.push_back(static_cast<double>(run.success_count));
    error_counts.push_back(static_cast<double>(run.error_count));
    bytes_sent.push_back(static_cast<double>(run.bytes_sent));
    bytes_received.push_back(static_cast<double>(run.bytes_received));
    rps.push_back(run.requests_per_second);
    mibps.push_back(run.mib_per_second);
    p50.push_back(run.latency_p50_us);
    p95.push_back(run.latency_p95_us);
    p99.push_back(run.latency_p99_us);
    max.push_back(run.latency_max_us);
  }

  aggregate.success_count = SummarizeScalar(success_counts);
  aggregate.error_count = SummarizeScalar(error_counts);
  aggregate.bytes_sent = SummarizeScalar(bytes_sent);
  aggregate.bytes_received = SummarizeScalar(bytes_received);
  aggregate.requests_per_second = SummarizeScalar(rps);
  aggregate.mib_per_second = SummarizeScalar(mibps);
  aggregate.latency_p50_us = SummarizeScalar(p50);
  aggregate.latency_p95_us = SummarizeScalar(p95);
  aggregate.latency_p99_us = SummarizeScalar(p99);
  aggregate.latency_max_us = SummarizeScalar(max);
  aggregate.stability = aggregate.requests_per_second.cv <= 0.10 &&
                                aggregate.latency_p95_us.cv <= 0.15
                            ? "stable"
                            : "unstable";
  return aggregate;
}

std::string CellKey(std::string_view scenario_name,
                    std::string_view pressure_name) {
  return std::string(scenario_name) + "::" + std::string(pressure_name);
}

}  // namespace bsrvcore::benchmark
