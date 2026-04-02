include(FetchContent)

set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT ON CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_FUZZ OFF CACHE BOOL "" FORCE)
set(MI_INSTALL_TOPLEVEL OFF CACHE BOOL "" FORCE)
set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  mimalloc
  GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
  GIT_TAG v3.2.8
)
FetchContent_MakeAvailable(mimalloc)

if(TARGET mimalloc-obj)
  set_property(TARGET mimalloc-obj PROPERTY POSITION_INDEPENDENT_CODE ON)
  if(NOT MSVC)
    target_compile_options(
      mimalloc-obj PRIVATE -Wno-error -Wno-pedantic -Wno-error=pedantic)
  endif()

  # Embed mimalloc object code into bsrvcore so exported static builds do not
  # depend on internal/unexported build targets.
  set(ALLOCATOR_OBJECT_SOURCES $<TARGET_OBJECTS:mimalloc-obj> PARENT_SCOPE)
else()
  message(FATAL_ERROR "Unable to find target 'mimalloc-obj' after FetchContent_MakeAvailable.")
endif()

add_library(bsrvcore_allocator_obj OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/allocator.cc
)
target_link_libraries(bsrvcore_allocator_obj PRIVATE bsrvcore_public_api)
target_include_directories(bsrvcore_allocator_obj PRIVATE
  ${mimalloc_SOURCE_DIR}/include
)
set_target_properties(bsrvcore_allocator_obj PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

set(BSRVCORE_OBJECT_TARGETS ${BSRVCORE_OBJECT_TARGETS} bsrvcore_allocator_obj
    PARENT_SCOPE)
