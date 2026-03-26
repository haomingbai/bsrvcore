#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include "benchmark_types.h"

namespace bsrvcore::benchmark {

RepetitionMetrics RunCellRepetition(const ScenarioDefinition& scenario,
                                    const PressureSettings& pressure,
                                    const RunSettings& run_settings,
                                    std::size_t repetition);
int RunServer(const ScenarioDefinition& scenario,
              const PressureSettings& pressure, std::string_view listen_host,
              unsigned short listen_port);

std::vector<CellResult> RunBenchmarks(
    const std::filesystem::path& executable_path,
    const std::vector<const ScenarioDefinition*>& selected_scenarios,
    const RunSettings& run_settings, ProfileKind profile);

}  // namespace bsrvcore::benchmark
