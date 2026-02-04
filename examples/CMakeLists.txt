# Add code check and optimization
if(MSVC)
  add_compile_options(/W4 /WX)   # MSVC: warning level 4 + treat warnings as errors
  # MSVC: Debug/Release small tweaks
  add_compile_options("$<$<CONFIG:Debug>:/Zi>")
  add_compile_options("$<$<CONFIG:Release>:/O2>")
  add_link_options("$<$<CONFIG:Debug>:/DEBUG>")
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror) # GCC/Clang

  # Debug: ASan + frame pointers
  # Release: -O3；RelWithDebInfo: -O2
  add_compile_options(
    "$<$<CONFIG:Debug>:-fsanitize=address>"
    "$<$<CONFIG:Debug>:-fno-omit-frame-pointer>"
    "$<$<CONFIG:Release>:-O3>"
    "$<$<CONFIG:RelWithDebInfo>:-O2>"
  )
  add_link_options("$<$<CONFIG:Debug>:-fsanitize=address>")
endif()

# example_quick_start
add_executable(example_quick_start example_quick_start.cc)
target_link_libraries(example_quick_start PRIVATE bsrvcore)

# example_configuration
add_executable(example_configuration example_configuration.cc)
target_link_libraries(example_configuration PRIVATE bsrvcore)

# example_oop_handler
add_executable(example_oop_handler example_oop_handler.cc)
target_link_libraries(example_oop_handler PRIVATE bsrvcore)

# example_aspect_basic
add_executable(example_aspect_basic example_aspect_basic.cc)
target_link_libraries(example_aspect_basic PRIVATE bsrvcore)

# example_logger_custom
add_executable(example_logger_custom example_logger_custom.cc)
target_link_libraries(example_logger_custom PRIVATE bsrvcore)

# example_session_context
add_executable(example_session_context example_session_context.cc)
target_link_libraries(example_session_context PRIVATE bsrvcore)

message(STATUS "Added bsrvcore examples")
