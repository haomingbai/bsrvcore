#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark_types.h"

namespace bsrvcore::benchmark {

inline constexpr std::size_t kMaxLatencySamples = 1'000'000;

std::string ToString(ProfileKind profile);
std::string ToString(PressureKind kind);
PressureKind ParsePressureKind(const std::string& value);
RunSettings ResolveRunSettings(const CliConfig& cli);
EnvironmentInfo DetectEnvironment();

std::string EscapeJson(std::string_view input);
std::string FormatDouble(double value, int precision = 3);
std::string Trim(std::string value);

double PercentileFromSorted(const std::vector<std::uint32_t>& sorted,
                            double fraction);
std::vector<std::uint32_t> SampleLatencies(
    const std::vector<std::uint32_t>& latencies);
ScalarSummary SummarizeScalar(const std::vector<double>& values);
AggregateMetrics AggregateRuns(const std::vector<RepetitionMetrics>& runs);
std::string CellKey(std::string_view scenario_name,
                    std::string_view pressure_name);

}  // namespace bsrvcore::benchmark
