# tests/CMakeLists.txt - bsrvcore tests
# Expect bsrvcore target to exist (we build tests after add_subdirectory(src))

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
FetchContent_MakeAvailable(googletest)

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
      ${CMAKE_SOURCE_DIR}/tests/support
  )

  add_test(NAME ${target_name} COMMAND ${target_name})
  set_tests_properties(${target_name} PROPERTIES
    LABELS ${label}
  )

  if(label STREQUAL "stress")
    # Stress tests in this project are for correctness and deadlock/livelock
    # detection, not for benchmark timing. Keep timeout generous.
    set_tests_properties(${target_name} PROPERTIES TIMEOUT 180)
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

# General tests (unit + integration).
add_subdirectory(general)

# Stress tests (optional).
if(BSRVCORE_ENABLE_STRESS_TESTS)
  add_subdirectory(stress)
endif()
