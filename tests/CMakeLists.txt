# tests/CMakeLists.txt - bsrvcore tests
# Expect bsrvcore target to exist (we build tests after add_subdirectory(src))

function(bsrvcore_add_test target_name source_file label)
  add_executable(${target_name} ${source_file})

  target_link_libraries(${target_name}
    PRIVATE
      bsrvcore
      GTest::gtest_main
      GTest::gmock
  )

  # Keep sanitizer settings consistent with library build (Debug config).
  if(NOT MSVC)
    target_compile_options(${target_name}
      PRIVATE
        "$<$<CONFIG:Debug>:-fsanitize=address>"
        "$<$<CONFIG:Debug>:-fno-omit-frame-pointer>"
    )
    target_link_options(${target_name}
      PRIVATE
        "$<$<CONFIG:Debug>:-fsanitize=address>"
    )
  endif()

  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_SOURCE_DIR}/src/include
      ${CMAKE_SOURCE_DIR}/tests/support
  )

  add_test(NAME ${target_name} COMMAND ${target_name})
  set_tests_properties(${target_name} PROPERTIES
    LABELS ${label}
  )

  if(label STREQUAL "stress")
    set_tests_properties(${target_name} PROPERTIES TIMEOUT 15)
  endif()
endfunction()

add_library(test_bsrvrun_handler_plugin SHARED
  ${CMAKE_SOURCE_DIR}/tests/support/bsrvrun_plugins/test_handler_plugin.cc
)
target_link_libraries(test_bsrvrun_handler_plugin PRIVATE bsrvcore)

add_library(test_bsrvrun_aspect_plugin SHARED
  ${CMAKE_SOURCE_DIR}/tests/support/bsrvrun_plugins/test_aspect_plugin.cc
)
target_link_libraries(test_bsrvrun_aspect_plugin PRIVATE bsrvcore)

# Unit tests
bsrvcore_add_test(server_configuration_test server_configuration_test.cc unit)
bsrvcore_add_test(unit_context_test unit_context_test.cc unit)
bsrvcore_add_test(unit_attribute_test unit_attribute_test.cc unit)
bsrvcore_add_test(unit_cookie_test unit_cookie_test.cc unit)
bsrvcore_add_test(unit_route_table_test unit_route_table_test.cc unit)
bsrvcore_add_test(unit_http_server_task_test unit_http_server_task_test.cc unit)
bsrvcore_add_test(unit_logger_test unit_logger_test.cc unit)
bsrvcore_add_test(unit_http_client_task_test unit_http_client_task_test.cc unit)
bsrvcore_add_test(unit_http_client_session_test unit_http_client_session_test.cc unit)
bsrvcore_add_test(unit_http_sse_client_task_test unit_http_sse_client_task_test.cc unit)
bsrvcore_add_test(unit_bsrvrun_config_test unit_bsrvrun_config_test.cc unit)

target_link_libraries(unit_bsrvrun_config_test PRIVATE bsrvrun_runtime)
target_include_directories(unit_bsrvrun_config_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src/bsrvrun
)

# Integration tests
bsrvcore_add_test(integration_http_server_test integration_http_server_test.cc integration)
bsrvcore_add_test(integration_bsrvrun_runtime_test integration_bsrvrun_runtime_test.cc integration)

target_link_libraries(integration_bsrvrun_runtime_test PRIVATE bsrvrun_runtime)
target_include_directories(integration_bsrvrun_runtime_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src/bsrvrun
)
target_compile_definitions(integration_bsrvrun_runtime_test PRIVATE
  BSRVRUN_TEST_HANDLER_PLUGIN="$<TARGET_FILE:test_bsrvrun_handler_plugin>"
  BSRVRUN_TEST_ASPECT_PLUGIN="$<TARGET_FILE:test_bsrvrun_aspect_plugin>"
)
add_dependencies(integration_bsrvrun_runtime_test
  test_bsrvrun_handler_plugin
  test_bsrvrun_aspect_plugin
)

# Stress tests (optional)
if(BSRVCORE_ENABLE_STRESS_TESTS)
  bsrvcore_add_test(stress_context_test stress_context_test.cc stress)
  bsrvcore_add_test(stress_server_post_test stress_server_post_test.cc stress)
  bsrvcore_add_test(stress_http_server_concurrency_test
    stress_http_server_concurrency_test.cc stress)
endif()

