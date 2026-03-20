#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark_types.h"

namespace bsrvcore::benchmark {

void PrintScenarioList(const std::vector<ScenarioDefinition>& scenarios);
void PrintCellSummary(const CellResult& cell);
std::string BuildJson(const EnvironmentInfo& environment, const CliConfig& cli,
                      const RunSettings& run_settings,
                      const std::vector<CellResult>& cells);
void WriteJsonFile(const std::filesystem::path& path, std::string_view content);

}  // namespace bsrvcore::benchmark
