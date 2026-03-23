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
  set(ALLOCATOR_INCLUDE_DIRS ${mimalloc_SOURCE_DIR}/include PARENT_SCOPE)
else()
  message(FATAL_ERROR "Unable to find target 'mimalloc-obj' after FetchContent_MakeAvailable.")
endif()

set(ALLOCATOR_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/allocator.cc
)
set(ALLOCATOR_SOURCES ${ALLOCATOR_SOURCES} PARENT_SCOPE)
