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

# Unit tests
bsrvcore_add_test(server_configuration_test server_configuration_test.cc unit)
bsrvcore_add_test(unit_context_test unit_context_test.cc unit)
bsrvcore_add_test(unit_attribute_test unit_attribute_test.cc unit)
bsrvcore_add_test(unit_cookie_test unit_cookie_test.cc unit)
bsrvcore_add_test(unit_route_table_test unit_route_table_test.cc unit)
bsrvcore_add_test(unit_http_server_task_test unit_http_server_task_test.cc unit)
bsrvcore_add_test(unit_logger_test unit_logger_test.cc unit)

# Integration tests
bsrvcore_add_test(integration_http_server_test integration_http_server_test.cc integration)

# Stress tests (optional)
if(BSRVCORE_ENABLE_STRESS_TESTS)
  bsrvcore_add_test(stress_context_test stress_context_test.cc stress)
  bsrvcore_add_test(stress_server_post_test stress_server_post_test.cc stress)
  bsrvcore_add_test(stress_http_server_concurrency_test
    stress_http_server_concurrency_test.cc stress)
endif()

