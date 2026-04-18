bsrvcore_add_test(unit_bsrvrun_config_test unit_bsrvrun_config_test.cc unit)
target_link_libraries(unit_bsrvrun_config_test PRIVATE bsrvrun_runtime)
target_include_directories(unit_bsrvrun_config_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src/bsrvrun
)

bsrvcore_add_test(integration_bsrvrun_runtime_test integration_bsrvrun_runtime_test.cc integration)
target_link_libraries(integration_bsrvrun_runtime_test PRIVATE bsrvrun_runtime)
target_include_directories(integration_bsrvrun_runtime_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src/bsrvrun
)
target_compile_definitions(integration_bsrvrun_runtime_test PRIVATE
  BSRVRUN_TEST_HANDLER_PLUGIN="$<TARGET_FILE:test_bsrvrun_handler_plugin>"
  BSRVRUN_TEST_ASPECT_PLUGIN="$<TARGET_FILE:test_bsrvrun_aspect_plugin>"
  BSRVRUN_TEST_SERVICE_PLUGIN="$<TARGET_FILE:test_bsrvrun_service_plugin>"
)
add_dependencies(integration_bsrvrun_runtime_test
  test_bsrvrun_handler_plugin
  test_bsrvrun_aspect_plugin
  test_bsrvrun_service_plugin
)
