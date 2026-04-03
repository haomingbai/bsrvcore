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

add_subdirectory(getting-started)
add_subdirectory(http-server)
add_subdirectory(sse)
add_subdirectory(routing)
add_subdirectory(aspects)
add_subdirectory(sessions-context)
add_subdirectory(logging)
add_subdirectory(client-tasks)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bsrvrun/plugins")
  add_subdirectory(bsrvrun/plugins)
endif()

message(STATUS "Added bsrvcore examples")
