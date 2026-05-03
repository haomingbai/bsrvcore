# Minimal src CMakeLists for bsrvcore (component-aware installs)
# - splits installed files into two CPack components: "runtime" and "devel"
# - runtime: shared libraries, runtime executables
# - devel: headers, static libs, CMake export/config files

# Add code check and optimization
option(BSRVCORE_ENABLE_ASAN "Enable AddressSanitizer for Debug builds. Default: OFF" OFF)
option(BSRVCORE_ENABLE_TSAN "Enable ThreadSanitizer for Debug/RelWithDebInfo builds. Default: OFF" OFF)
include(FetchContent)

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

set(BSRVCORE_WINDOWS_COMPILE_DEFINITIONS)
if(WIN32)
  list(APPEND BSRVCORE_WINDOWS_COMPILE_DEFINITIONS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
  )
endif()

if(MSVC)
  add_compile_options(/bigobj)
  # Windows CI is best-effort today: keep Debug strict, but make release-like
  # configs warning-tolerant so vendored deps and low-priority MSVC issues do
  # not block packaging and test builds.
  add_compile_options(
    "$<$<CONFIG:Debug>:/W4>"
    "$<$<CONFIG:Debug>:/WX>"
    "$<$<CONFIG:Debug>:/Zi>"
    "$<$<NOT:$<CONFIG:Debug>>:/W2>"
    "$<$<CONFIG:Release>:/O2>"
  )
  add_link_options("$<$<CONFIG:Debug>:/DEBUG>")
else()
  add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    "$<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>"
  ) # GCC/Clang

  # Release: -O3；RelWithDebInfo: -O2
  add_compile_options(
    "$<$<CONFIG:Release>:-O3>"
    "$<$<CONFIG:RelWithDebInfo>:-O2>"
  )

  if(BSRVCORE_ENABLE_ASAN)
    # ASan requires frame pointers for high-quality stacks.
    add_compile_options(
      "$<$<CONFIG:Debug>:-fsanitize=address>"
      "$<$<CONFIG:Debug>:-fno-omit-frame-pointer>"
    )
    add_link_options("$<$<CONFIG:Debug>:-fsanitize=address>")
  endif()

  if(BSRVCORE_ENABLE_TSAN)
    add_compile_options(
      "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-fsanitize=thread>"
      "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-fno-omit-frame-pointer>"
    )
    add_link_options(
      "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-fsanitize=thread>"
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      add_compile_options(-Wno-error=tsan)
    endif()
  endif()
endif()

# --------------------------------------------------------
# Internal build-time dependencies (src scope only)
# --------------------------------------------------------
FetchContent_Declare(
  bthpool
  GIT_REPOSITORY https://github.com/haomingbai/bthpool.git
  GIT_TAG v0.4.1
)
FetchContent_GetProperties(bthpool)
if(NOT bthpool_POPULATED)
  FetchContent_Populate(bthpool)
endif()

add_library(bthpool_headers INTERFACE)
add_library(bthpool::headers ALIAS bthpool_headers)
target_include_directories(bthpool_headers SYSTEM INTERFACE
  ${bthpool_SOURCE_DIR}/include
)
target_compile_definitions(bthpool_headers INTERFACE
  BTHPOOL_HAS_BOOST_ASIO=1
  USE_BOOST_ASIO_EXECUTOR
)

set(BTHPOOL_INCLUDE_DIR "${bthpool_SOURCE_DIR}/include")

set(BSRVCORE_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../include")

add_library(bsrvcore_public_api INTERFACE)
target_compile_features(bsrvcore_public_api INTERFACE cxx_std_20)
target_include_directories(bsrvcore_public_api INTERFACE
  ${BSRVCORE_PUBLIC_INCLUDE_DIR}
)
target_compile_definitions(bsrvcore_public_api INTERFACE
  ${BSRVCORE_WINDOWS_COMPILE_DEFINITIONS}
)

if(TARGET Boost::headers)
  set(BSRVCORE_BOOST_HEADERS_TARGET Boost::headers)
elseif(TARGET Boost::boost)
  set(BSRVCORE_BOOST_HEADERS_TARGET Boost::boost)
else()
  message(FATAL_ERROR
    "Boost headers target not found (expected Boost::headers or Boost::boost).")
endif()

target_link_libraries(bsrvcore_public_api INTERFACE
  ${BSRVCORE_BOOST_HEADERS_TARGET}
  OpenSSL::SSL
  OpenSSL::Crypto
)

set(BSRVCORE_OBJECT_TARGETS)

# Add the subdirectories in this project.
option(BSRVCORE_BUILD_TOOLS "Build bsrvcore runtime tools. Default: ON" ON)

add_subdirectory(allocator)
add_subdirectory(file)
add_subdirectory(route)
add_subdirectory(session)
add_subdirectory(connection)
add_subdirectory(core)
add_subdirectory(bsrvrun)

# Create library (static or shared follows BUILD_SHARED_LIBS)
add_library(bsrvcore)
add_library(bsrvcore::bsrvcore ALIAS bsrvcore)

foreach(object_target IN LISTS BSRVCORE_OBJECT_TARGETS)
  target_sources(bsrvcore PRIVATE $<TARGET_OBJECTS:${object_target}>)
endforeach()

if(DEFINED ALLOCATOR_OBJECT_SOURCES)
  target_sources(bsrvcore PRIVATE ${ALLOCATOR_OBJECT_SOURCES})
endif()

# Ensure the installed target advertises the project language level.
target_compile_features(bsrvcore PUBLIC cxx_std_20)
target_compile_definitions(bsrvcore
  PUBLIC
    ${BSRVCORE_WINDOWS_COMPILE_DEFINITIONS}
)

# OpenSSL and Boost.Json stay PUBLIC because installed headers expose
# aliases from bsrvcore/core/types.h that wrap SslContext and boost::json.
target_link_libraries(bsrvcore
  PRIVATE
    Boost::url
    Boost::regex
  PUBLIC
    ${BSRVCORE_BOOST_HEADERS_TARGET}
    Boost::json
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(TARGET Boost::system)
  target_link_libraries(bsrvcore PRIVATE Boost::system)
endif()

# Keep bthpool as a pure internal build dependency; this avoids exporting
# an internal-only target when bsrvcore is built as a static library.
target_include_directories(bsrvcore SYSTEM PRIVATE
  ${BTHPOOL_INCLUDE_DIR}
)
target_compile_definitions(bsrvcore PRIVATE
  BTHPOOL_HAS_BOOST_ASIO=1
  USE_BOOST_ASIO_EXECUTOR
)

# Public headers stay under the top-level include/ tree so installed consumers
# only see the supported API surface. Internal headers now live under each
# module's src/<module>/include* trees and are consumed through object-library
# usage requirements during the build only.
target_include_directories(bsrvcore
  PUBLIC
    $<BUILD_INTERFACE:${BSRVCORE_PUBLIC_INCLUDE_DIR}>          # project headers for build
    $<INSTALL_INTERFACE:include>                               # project headers for install
)

# Set version and export name
set_target_properties(bsrvcore PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME bsrvcore
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)

# Position independent code for non-Windows
if(NOT WIN32)
  set_target_properties(bsrvcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(BSRVCORE_BUILD_C_BINDINGS)
  add_subdirectory(c_binding)
endif()

# ------------------------------------------------------------------
# Component-aware install rules
# - "runtime" component: shared libraries and runtime executables
# - "devel" component: headers, static libs (archive), CMake export file
# NOTE: Top-level CMakeLists.txt should set INSTALL_CONFIGDIR (used below).
# If not defined, provide a safe default.
# ------------------------------------------------------------------
if(NOT DEFINED INSTALL_CONFIGDIR)
  set(INSTALL_CONFIGDIR "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")
endif()

# Install the target and mark files to components
install(TARGETS bsrvcore
  EXPORT bsrvcoreTargets
  # Shared libraries (.so) and platform runtime files go into runtime
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  # Static archives (.a) and import libraries go to devel (developer package)
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT devel
  # headers and INCLUDE location hint (the actual headers are installed below)
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install public C++ headers into the core devel component. C binding headers
# are installed by src/c_binding into their own c_devel component.
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/bsrvcore/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bsrvcore
  COMPONENT devel
  FILES_MATCHING PATTERN "*.h*"
)

# ------------------------ pkg-config generation ------------------------
# Generate a relocatable pkg-config file for bsrvcore and install it with
# the devel component.
if(NOT DEFINED PROJECT_DESCRIPTION OR PROJECT_DESCRIPTION STREQUAL "")
  set(BSRVCORE_PC_DESCRIPTION "${PROJECT_NAME} HTTP server library")
else()
  set(BSRVCORE_PC_DESCRIPTION "${PROJECT_DESCRIPTION}")
endif()

# pkg-config must expose TLS and Boost.Json publicly because consumer
# translation units can directly instantiate those public API types.
set(BSRVCORE_PC_PUBLIC_LIBS "-lssl -lcrypto -lboost_json")
set(BSRVCORE_PC_PRIVATE_LIBS "-lboost_system -lboost_url")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/bsrvcore.pc.in
               ${CMAKE_CURRENT_BINARY_DIR}/bsrvcore.pc @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bsrvcore.pc
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig
  COMPONENT devel
)
# ----------------------------------------------------------------------

# Export installed CMake targets so other projects can find this package
install(EXPORT bsrvcoreTargets
  FILE bsrvcoreTargets.cmake
  NAMESPACE bsrvcore::
  DESTINATION ${INSTALL_CONFIGDIR}
  COMPONENT devel
)

# Note: the top-level project usually configures and installs
# bsrvcoreConfig.cmake and bsrvcoreConfigVersion.cmake (configured files).
# Make sure those top-level install(FILES ...) calls use COMPONENT devel
# so the -dev/-devel package contains the CMake config files.

# Export a build-tree package so sibling libraries can resolve bsrvcore from a
# configured build directory via -Dbsrvcore_DIR=<build-dir>.
export(EXPORT bsrvcoreTargets
  FILE "${PROJECT_BINARY_DIR}/bsrvcoreTargets.cmake"
  NAMESPACE bsrvcore::
)

# ------------------------------------------------------------------
# Optional: generate a pkg-config file at install time (uncomment if needed)
# configure_file(${CMAKE_CURRENT_SOURCE_DIR}/pkgconfig/bsrvcore.pc.in
#                ${CMAKE_CURRENT_BINARY_DIR}/bsrvcore.pc @ONLY)
# install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bsrvcore.pc
#   DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig COMPONENT devel)
# ------------------------------------------------------------------
