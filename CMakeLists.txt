cmake_minimum_required(VERSION 3.25)
project(bsrvcore VERSION 0.17.0 LANGUAGES C CXX)

if(POLICY CMP0167)
  # Keep CMP0167 scoped to Apple toolchains only: Homebrew Boost can miss
  # per-component config packages (for example boost_systemConfig.cmake).
  # Other platforms keep the default policy behavior.
  if(APPLE)
    cmake_policy(SET CMP0167 OLD)
  else()
    cmake_policy(SET CMP0167 NEW)
  endif()
endif()

# Keep historical default (shared) on Unix-like platforms while still allowing
# callers/subprojects to fully control the library type via BUILD_SHARED_LIBS.
# Windows support is intentionally best-effort for now, so default to static
# there and avoid DLL export/import library issues in CI.
if(NOT DEFINED BUILD_SHARED_LIBS)
  if(WIN32)
    option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
  else()
    option(BUILD_SHARED_LIBS "Build shared libraries" ON)
  endif()
endif()

# Use C++20
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Basic -Wall only at project level.
# Strict warnings (-Wextra -Werror -Wnon-virtual-dtor) are set in
# src/CMakeLists.txt for bsrvcore's own code only, so third-party
# dependencies (GoogleTest, yaml-cpp, mimalloc) are not affected.
if(NOT MSVC)
  add_compile_options(-Wall)
endif()

# Generate compile_commands.json to help LSP server
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Require Boost headers for the public API, plus linkable components used
# internally and by the bsrvrun CLI.
if(APPLE)
  # Homebrew Boost may not ship per-component BoostConfig files.
  set(Boost_NO_BOOST_CMAKE ON)
  find_package(Boost 1.75 REQUIRED COMPONENTS url program_options json regex)
else()
  find_package(Boost 1.75 REQUIRED COMPONENTS system url program_options json regex)
endif()

# OpenSSL is part of the public developer-facing dependency surface because
# installed headers expose boost::asio::ssl::context in public APIs.
find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)

# Standard install directories
include(GNUInstallDirs)

# Helpers to generate package config files
include(CMakePackageConfigHelpers)

option(BSRVCORE_BUILD_C_BINDINGS "Build bsrvcore C bindings. Default: ON" ON)

# Build library from src
add_subdirectory(src)

# Optional: Build examples
option(BSRVCORE_BUILD_EXAMPLES "Build bsrvcore examples. Default: ON" ON)
option(BSRVCORE_BUILD_BENCHMARKS "Build bsrvcore benchmarks. Default: OFF" OFF)

if(BSRVCORE_BUILD_EXAMPLES)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_subdirectory(examples)
  else()
    message(STATUS "BSRVCORE_BUILD_EXAMPLES is ON but the 'examples' directory does not exist.")
  endif()
endif()

if(BSRVCORE_BUILD_BENCHMARKS)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks")
    add_subdirectory(benchmarks)
  else()
    message(STATUS "BSRVCORE_BUILD_BENCHMARKS is ON but the 'benchmarks' directory does not exist.")
  endif()
endif()

# Directory to install config files
set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/bsrvcore)

# Generate a config file from template
configure_package_config_file(
  ${CMAKE_CURRENT_LIST_DIR}/cmake/bsrvcoreConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/bsrvcoreConfig.cmake
  INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

# Write a basic package version file
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/bsrvcoreConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

# Install generated config files
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/bsrvcoreConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/bsrvcoreConfigVersion.cmake
  DESTINATION ${INSTALL_CONFIGDIR}
  COMPONENT devel
)

# Optional Doxygen: generate docs only if Doxygen is found
find_package(Doxygen QUIET)

if(DOXYGEN_FOUND)
  # Output dir for docs
  set(DOXYGEN_OUTPUT_DIR "${PROJECT_BINARY_DIR}/docs")
  # Input dirs for Doxygen
  set(DOXYGEN_INPUT_DIRS "${PROJECT_SOURCE_DIR}/include")

  # Configure Doxyfile from template
  configure_file(${CMAKE_CURRENT_LIST_DIR}/cmake/Doxyfile.in
                 ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)

  # Add doc target that runs Doxygen
  add_custom_target(doc
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generate API docs with Doxygen"
    VERBATIM
  )
else()
  # Notify user that docs are skipped
  message(STATUS "Doxygen not found; skipping doc target")
endif()

# CPack support
include(InstallRequiredSystemLibraries)

# Basic package metadata
set(BSRVCORE_CPACK_GENERATORS "DEB;RPM;TGZ" CACHE STRING
  "CPack generators to use for bsrvcore release packages")
set(BSRVCORE_PACKAGE_NAME_SUFFIX "" CACHE STRING
  "Optional suffix appended to the CPack package name")

set(CPACK_GENERATOR "${BSRVCORE_CPACK_GENERATORS}")
set(CPACK_PACKAGE_NAME    "${PROJECT_NAME}${BSRVCORE_PACKAGE_NAME_SUFFIX}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "haomingbai@hotmail.com")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# Component behaviour: create one package per component (recommended)
# ONE_PER_GROUP will create one package per component (no grouping).
set(CPACK_COMPONENTS_GROUPING "ONE_PER_GROUP")

# Enable component-aware packaging for DEB and RPM
set(CPACK_DEB_COMPONENT_INSTALL ON)   # DEB: produce multiple .deb (e.g. package and package-dev)
set(CPACK_RPM_COMPONENT_INSTALL ON)   # RPM: produce multiple .rpm (e.g. package and package-devel)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)  # TGZ: produce one archive per component

# Which components we define (order is presentation order)
# Note: install(... COMPONENT ...) should use these names ("runtime", "devel")
if(BUILD_SHARED_LIBS)
  set(CPACK_COMPONENTS_ALL runtime devel)
  if(BSRVCORE_BUILD_C_BINDINGS)
    list(APPEND CPACK_COMPONENTS_ALL c_devel)
  endif()
else()
  # Static-only builds don't install runtime shared objects.
  set(CPACK_COMPONENTS_ALL devel)
  if(BSRVCORE_BUILD_C_BINDINGS)
    list(APPEND CPACK_COMPONENTS_ALL c_devel)
  endif()
endif()

# Human-friendly names & descriptions shown by some installers
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Runtime libraries")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "Shared libraries required to run ${PROJECT_NAME}.")
set(CPACK_COMPONENT_DEVEL_DISPLAY_NAME "Development files")
set(CPACK_COMPONENT_DEVEL_DESCRIPTION "Headers, CMake config and pkg-config files for developing against ${PROJECT_NAME}.")
if(BSRVCORE_BUILD_C_BINDINGS)
  set(CPACK_COMPONENT_C_DEVEL_DISPLAY_NAME "C binding development files")
  set(CPACK_COMPONENT_C_DEVEL_DESCRIPTION "C headers, CMake config and pkg-config files for developing against the bsrvcore C bindings.")
endif()

# Let dpkg-shlibdeps compute runtime deps automatically for DEB
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Bind shlibdeps specifically to the runtime component (so found deps go into the runtime package)
set(CPACK_DEBIAN_RUNTIME_PACKAGE_SHLIBDEPS ON)

# Per-component package names (upper-case component id in variable name)
# Debian naming: we want e.g. bsrvcore_<ver> and bsrvcore-dev_<ver>
set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_DEBIAN_DEVEL_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-dev")
if(BSRVCORE_BUILD_C_BINDINGS)
  set(CPACK_DEBIAN_C_DEVEL_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-c-dev")
endif()

# Ensure devel package metadata pulls required development dependencies.
# - Public headers expose Boost.Asio/Beast and OpenSSL types.
# - Linking against bsrvcore also needs Boost.System and Boost.URL dev files.
if(BUILD_SHARED_LIBS)
  set(CPACK_DEBIAN_DEVEL_PACKAGE_DEPENDS
    "${CPACK_DEBIAN_RUNTIME_PACKAGE_NAME} (= ${CPACK_PACKAGE_VERSION}), libboost-dev, libboost-system-dev, libboost-url-dev, libboost-json-dev, libssl-dev"
  )
else()
  set(CPACK_DEBIAN_DEVEL_PACKAGE_DEPENDS
    "libboost-dev, libboost-system-dev, libboost-url-dev, libboost-json-dev, libssl-dev"
  )
endif()
if(BSRVCORE_BUILD_C_BINDINGS)
  if(BUILD_SHARED_LIBS)
    set(CPACK_DEBIAN_C_DEVEL_PACKAGE_DEPENDS
      "${CPACK_DEBIAN_RUNTIME_PACKAGE_NAME} (= ${CPACK_PACKAGE_VERSION})"
    )
  else()
    set(CPACK_DEBIAN_C_DEVEL_PACKAGE_DEPENDS "")
  endif()
endif()

# RPM naming: typical rpm convention uses "-devel"
set(CPACK_RPM_RUNTIME_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_RPM_DEVEL_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-devel")
if(BSRVCORE_BUILD_C_BINDINGS)
  set(CPACK_RPM_C_DEVEL_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-c-devel")
endif()

# Ensure devel RPM declares toolchain-level development dependencies.
# boost-devel provides the public Boost.Asio/Beast headers and component libs.
if(BUILD_SHARED_LIBS)
  set(CPACK_RPM_DEVEL_PACKAGE_REQUIRES "${CPACK_RPM_RUNTIME_PACKAGE_NAME} = ${CPACK_PACKAGE_VERSION}, boost-devel, openssl-devel")
else()
  set(CPACK_RPM_DEVEL_PACKAGE_REQUIRES "boost-devel, openssl-devel")
endif()
if(BSRVCORE_BUILD_C_BINDINGS)
  if(BUILD_SHARED_LIBS)
    set(CPACK_RPM_C_DEVEL_PACKAGE_REQUIRES "${CPACK_RPM_RUNTIME_PACKAGE_NAME} = ${CPACK_PACKAGE_VERSION}")
  else()
    set(CPACK_RPM_C_DEVEL_PACKAGE_REQUIRES "")
  endif()
endif()

# Refresh linker cache on install/uninstall so newly installed libbsrvcore.so.N
# can be found immediately by runtime loaders.
set(CPACK_RPM_RUNTIME_POST_INSTALL_SCRIPT_FILE
  "${PROJECT_SOURCE_DIR}/cmake/cpack/rpm/runtime-post.sh"
)
set(CPACK_RPM_RUNTIME_POST_UNINSTALL_SCRIPT_FILE
  "${PROJECT_SOURCE_DIR}/cmake/cpack/rpm/runtime-postun.sh"
)

# Optional: short summaries for each RPM package
set(CPACK_RPM_RUNTIME_PACKAGE_SUMMARY "Runtime libraries for ${PROJECT_NAME}")
set(CPACK_RPM_DEVEL_PACKAGE_SUMMARY "Development files (headers & cmake config) for ${PROJECT_NAME}")
if(BSRVCORE_BUILD_C_BINDINGS)
  set(CPACK_RPM_C_DEVEL_PACKAGE_SUMMARY "Development files for the ${PROJECT_NAME} C bindings")
endif()

# If you need to set Debian package architecture explicitly:
# set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")

# Resource files
if(EXISTS "${PROJECT_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
endif()

# Useful: avoid packing -dev files into runtime by mistake:
# make sure any install() that places headers / cmake config uses COMPONENT devel.
# Conversely, install shared libraries/binaries with COMPONENT runtime.

include(CPack)

# --------------------------------------------------------
# CTest / GoogleTest (minimal)
# --------------------------------------------------------
option(BSRVCORE_BUILD_TESTS "Build bsrvcore tests (requires GoogleTest). Default: OFF" OFF)
option(BSRVCORE_ENABLE_STRESS_TESTS "Enable bsrvcore stress tests. Default: ON" ON)

if(BSRVCORE_BUILD_TESTS)
  enable_testing()

  # add tests subdir (create tests/CMakeLists.txt below)
  add_subdirectory(tests)
endif()
