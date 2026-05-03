#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace bsrvcore {
namespace benchmark {
struct CellResult;
struct CliConfig;
struct EnvironmentInfo;
struct RunSettings;
struct ScenarioDefinition;
}  // namespace benchmark
}  // namespace bsrvcore

namespace bsrvcore::benchmark {

void PrintScenarioList(const std::vector<ScenarioDefinition>& scenarios);
void PrintCellSummary(const CellResult& cell);
std::string BuildJson(const EnvironmentInfo& environment, const CliConfig& cli,
                      const RunSettings& run_settings,
                      const std::vector<CellResult>& cells);
void WriteJsonFile(const std::filesystem::path& path, std::string_view content);

}  // namespace bsrvcore::benchmark
