#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "benchmark_cli.h"
#include "benchmark_report.h"
#include "benchmark_runner.h"
#include "benchmark_scenarios.h"
#include "benchmark_subprocess.h"
#include "benchmark_types.h"
#include "benchmark_util.h"

int main(int argc, char** argv) try {
  using namespace bsrvcore::benchmark;

  std::string help_text;
  const CliConfig cli = ParseCli(argc, argv, help_text);
  if (cli.show_help) {
    std::cout << help_text << "\n";
    return 0;
  }

  const auto scenarios = BuildScenarios();
  if (cli.internal_run_cell) {
    return RunInternalCell(cli, scenarios);
  }
  if (cli.list_scenarios) {
    PrintScenarioList(scenarios);
    return 0;
  }

  if (cli.mode == RunMode::kServer) {
    if (cli.scenario_name == "all" || cli.scenario_name == "io") {
      throw std::invalid_argument(
          "server mode requires one named scenario, not 'all' or 'io'");
    }
    const auto run_settings = ResolveRunSettings(cli);
    if (run_settings.pressures.size() != 1) {
      throw std::invalid_argument(
          "server mode requires one explicit pressure configuration");
    }
    if (!cli.listen_port.has_value()) {
      throw std::invalid_argument("server mode requires --listen-port");
    }
    const auto& scenario = FindScenario(scenarios, cli.scenario_name);
    return RunServer(scenario, run_settings.pressures.front(), cli.listen_host,
                     *cli.listen_port, run_settings);
  }

  const auto selected_scenarios = ResolveSelectedScenarios(scenarios, cli);
  const auto run_settings = ResolveRunSettings(cli);
  const auto environment = DetectEnvironment();
  const auto executable_path = std::filesystem::absolute(argv[0]);
  const auto cells = RunBenchmarks(executable_path, selected_scenarios,
                                   run_settings, cli.profile);

  for (const auto& cell : cells) {
    PrintCellSummary(cell);
  }

  const auto json = BuildJson(environment, cli, run_settings, cells);
  if (cli.output_json.has_value()) {
    WriteJsonFile(*cli.output_json, json);
  }

  return 0;
} catch (const std::exception& ex) {
  std::cerr << "benchmark failed: " << ex.what() << "\n";
  return 1;
}
