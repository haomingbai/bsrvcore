#pragma once

#include <filesystem>
#include <vector>

#include "benchmark_types.h"

namespace bsrvcore::benchmark {

int RunInternalCell(const CliConfig& cli,
                    const std::vector<ScenarioDefinition>& scenarios);

RepetitionMetrics RunCellInSubprocess(
    const std::filesystem::path& executable_path,
    const ScenarioDefinition& scenario, const PressureSettings& pressure,
    std::size_t warmup_ms, std::size_t duration_ms, std::size_t cooldown_ms,
    std::size_t repetition);

}  // namespace bsrvcore::benchmark
