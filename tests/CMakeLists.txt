# tests/CMakeLists.txt - minimal tests
# Expect bsrvcore target to exist (we build tests after add_subdirectory(src))

add_executable(server_configuration_test
  server_configuration_test.cpp
)

target_link_libraries(server_configuration_test
  PRIVATE
    bsrvcore        # link your library to test it
    GTest::gtest_main
)

# ensure test can find public headers
target_include_directories(server_configuration_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_test(NAME server_configuration_test COMMAND server_configuration_test)
set_tests_properties(server_configuration_test PROPERTIES COMMAND_ARGUMENTS "--rerun-failed;--output-on-failure")

