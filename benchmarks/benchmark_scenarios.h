#pragma once

#include <string_view>
#include <vector>

#include "benchmark_types.h"

namespace bsrvcore::benchmark {

std::vector<ScenarioDefinition> BuildScenarios();
const ScenarioDefinition& FindScenario(
    const std::vector<ScenarioDefinition>& scenarios, std::string_view name);
std::vector<const ScenarioDefinition*> ResolveSelectedScenarios(
    const std::vector<ScenarioDefinition>& scenarios, const CliConfig& cli);

}  // namespace bsrvcore::benchmark
