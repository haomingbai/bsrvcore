add_executable(bsrvcore_http_benchmark
  benchmark_client.cc
  benchmark_cli.cc
  benchmark_main.cc
  benchmark_report.cc
  benchmark_runner.cc
  benchmark_scenarios.cc
  benchmark_subprocess.cc
  benchmark_util.cc
)

target_compile_features(bsrvcore_http_benchmark PRIVATE cxx_std_23)

target_link_libraries(bsrvcore_http_benchmark
  PRIVATE
    bsrvcore
    Boost::headers
    Boost::system
    Boost::program_options
)

target_include_directories(bsrvcore_http_benchmark
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(bsrvcore_http_benchmark
  PRIVATE
    BSRVCORE_BENCHMARK_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
    BSRVCORE_BENCHMARK_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    BSRVCORE_BENCHMARK_BUILD_TYPE="$<CONFIG>"
)
