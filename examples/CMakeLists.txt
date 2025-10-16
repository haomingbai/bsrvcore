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

# quick_start
add_executable(quick_start quick_start.cc)
target_link_libraries(quick_start PRIVATE bsrvcore)

# use_logger
add_executable(use_logger use_logger.cc)
target_link_libraries(use_logger PRIVATE bsrvcore)

# oop_developing
add_executable(oop_developing oop_developing.cc)
target_link_libraries(oop_developing PRIVATE bsrvcore)

# Optional: Improve user experience by printing a message
message(STATUS "Added example: simple_example")
