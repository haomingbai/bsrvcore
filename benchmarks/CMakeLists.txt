include(ExternalProject)

add_executable(bsrvcore_http_benchmark
  benchmark_cli.cc
  benchmark_main.cc
  benchmark_report.cc
  benchmark_runner.cc
  benchmark_scenarios.cc
  benchmark_subprocess.cc
  benchmark_util.cc
)

target_compile_features(bsrvcore_http_benchmark PRIVATE cxx_std_23)

option(BSRVCORE_BENCHMARK_BUILD_BUNDLED_WRK
       "Build bundled wrk for benchmark load generation. Default: OFF"
       OFF)

set(BSRVCORE_BENCHMARK_WRK_DEFAULT "")
if(BSRVCORE_BENCHMARK_BUILD_BUNDLED_WRK AND UNIX)
  ExternalProject_Add(bsrvcore_benchmark_wrk
    PREFIX ${CMAKE_BINARY_DIR}/_deps/bsrvcore_benchmark_wrk
    GIT_REPOSITORY https://github.com/wg/wrk.git
    GIT_TAG 4.2.0
    GIT_SHALLOW 1
    UPDATE_DISCONNECTED 1
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE 1
    BUILD_COMMAND make
    INSTALL_COMMAND ""
    TEST_COMMAND ""
  )
  ExternalProject_Get_Property(bsrvcore_benchmark_wrk SOURCE_DIR)
  set(BSRVCORE_BENCHMARK_WRK_DEFAULT "${SOURCE_DIR}/wrk")
  add_dependencies(bsrvcore_http_benchmark bsrvcore_benchmark_wrk)
endif()

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
    BSRVCORE_BENCHMARK_WRK_DEFAULT="${BSRVCORE_BENCHMARK_WRK_DEFAULT}"
)
