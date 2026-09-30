# Add code check and optimization
if(MSVC)
  add_compile_options(/W4 /WX)   # MSVC: warning level 4 + treat warnings as errors
  # MSVC: Debug/Release small tweaks
  add_compile_options("$<$<CONFIG:Debug>:/Zi>")
  add_compile_options("$<$<CONFIG:Release>:/O2>")
  add_link_options("$<$<CONFIG:Debug>:/DEBUG>")
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror) # GCC/Clang

  # Sanitizer used by the example targets. Disabled by default so local
  # Debug builds and test runs stay sanitizer-free; opt in explicitly with
  # "address" (historical behavior) or "thread" when building the whole
  # project with BSRVCORE_ENABLE_TSAN=ON.
  set(BSRVCORE_EXAMPLES_SANITIZER "none" CACHE STRING
    "Sanitizer for example targets: address, thread or none. Default: none")
  set_property(CACHE BSRVCORE_EXAMPLES_SANITIZER PROPERTY
    STRINGS address thread none)

  # Release: -O3；RelWithDebInfo: -O2
  add_compile_options(
    "$<$<CONFIG:Release>:-O3>"
    "$<$<CONFIG:RelWithDebInfo>:-O2>"
  )

  if(BSRVCORE_EXAMPLES_SANITIZER STREQUAL "address")
    # Debug: ASan + frame pointers
    add_compile_options(
      "$<$<CONFIG:Debug>:-fsanitize=address>"
      "$<$<CONFIG:Debug>:-fno-omit-frame-pointer>"
    )
    add_link_options("$<$<CONFIG:Debug>:-fsanitize=address>")
  elseif(BSRVCORE_EXAMPLES_SANITIZER STREQUAL "thread")
    # Match the TSan scope used by src/CMakeLists.txt (Debug/RelWithDebInfo)
    # so examples stay link-compatible with a global TSan build.
    add_compile_options(
      "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-fsanitize=thread>"
      "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-fno-omit-frame-pointer>"
    )
    add_link_options(
      "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-fsanitize=thread>"
    )
  elseif(NOT BSRVCORE_EXAMPLES_SANITIZER STREQUAL "none")
    message(FATAL_ERROR
      "BSRVCORE_EXAMPLES_SANITIZER must be 'address', 'thread' or 'none', "
      "got '${BSRVCORE_EXAMPLES_SANITIZER}'.")
  endif()
endif()

add_subdirectory(getting-started)
add_subdirectory(http-server)
add_subdirectory(sse)
add_subdirectory(routing)
add_subdirectory(aspects)
add_subdirectory(sessions-context)
add_subdirectory(logging)
add_subdirectory(client-tasks)
add_subdirectory(websocket-tasks)

if(TARGET bsrvcore_c)
  add_subdirectory(c-binding)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bsrvrun/plugins")
  add_subdirectory(bsrvrun/plugins)
endif()

message(STATUS "Added bsrvcore examples")
