include(FetchContent)

set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
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

add_library(bsrvcore_mimalloc_allocator_backend INTERFACE)
if(TARGET mimalloc-static)
  target_link_libraries(
    bsrvcore_mimalloc_allocator_backend INTERFACE mimalloc-static)
  set_property(TARGET mimalloc-static PROPERTY POSITION_INDEPENDENT_CODE ON)
  if(NOT MSVC)
    target_compile_options(
      mimalloc-static PRIVATE -Wno-error -Wno-pedantic -Wno-error=pedantic)
  endif()
elseif(TARGET mimalloc)
  target_link_libraries(
    bsrvcore_mimalloc_allocator_backend INTERFACE mimalloc)
  set_property(TARGET mimalloc PROPERTY POSITION_INDEPENDENT_CODE ON)
  if(NOT MSVC)
    target_compile_options(
      mimalloc PRIVATE -Wno-error -Wno-pedantic -Wno-error=pedantic)
  endif()
else()
  message(FATAL_ERROR "Unable to find a mimalloc target after FetchContent_MakeAvailable.")
endif()

set(ALLOCATOR_DEP_LIBS bsrvcore_mimalloc_allocator_backend PARENT_SCOPE)

set(ALLOCATOR_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/allocator.cc
)
set(ALLOCATOR_SOURCES ${ALLOCATOR_SOURCES} PARENT_SCOPE)
