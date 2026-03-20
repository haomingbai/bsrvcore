#pragma once

#include <string>

#include "benchmark_types.h"

namespace bsrvcore::benchmark {

CliConfig ParseCli(int argc, char** argv, std::string& help_text);

}  // namespace bsrvcore::benchmark
