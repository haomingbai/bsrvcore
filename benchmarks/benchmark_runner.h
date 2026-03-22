#pragma once

#include <filesystem>
#include <vector>

#include "benchmark_types.h"

namespace bsrvcore::benchmark {

RepetitionMetrics RunCellRepetition(const ScenarioDefinition& scenario,
                                    const PressureSettings& pressure,
                                    const RunSettings& run_settings,
                                    std::size_t repetition);

std::vector<CellResult> RunBenchmarks(
    const std::filesystem::path& executable_path,
    const std::vector<const ScenarioDefinition*>& selected_scenarios,
    const RunSettings& run_settings, ProfileKind profile);

}  // namespace bsrvcore::benchmark
