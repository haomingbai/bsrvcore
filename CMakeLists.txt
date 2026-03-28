cmake_minimum_required(VERSION 3.25)
project(bsrvcore VERSION 0.9.2 LANGUAGES CXX)

# Keep historical default (shared) while still allowing callers/subprojects
# to fully control the library type via BUILD_SHARED_LIBS.
if(NOT DEFINED BUILD_SHARED_LIBS)
  option(BUILD_SHARED_LIBS "Build shared libraries" ON)
endif()

# Use C++23
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Add Wall flag.
if(NOT MSVC)
  add_compile_options(-Wall)
endif()

# Generate compile_commands.json to help LSP server
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Require Boost headers for the public API, plus linkable components used
# internally and by the bsrvrun CLI.
find_package(Boost 1.75 COMPONENTS headers system url program_options CONFIG REQUIRED)

# OpenSSL is part of the public developer-facing dependency surface because
# installed headers expose boost::asio::ssl::context in public APIs.
find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)

# Standard install directories
include(GNUInstallDirs)

# Helpers to generate package config files
include(CMakePackageConfigHelpers)

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

# Install public headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  COMPONENT devel
)

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
  set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")
  # Input dirs for Doxygen
  set(DOXYGEN_INPUT_DIRS "${CMAKE_SOURCE_DIR}/include")

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
set(CPACK_GENERATOR "DEB;RPM;TGZ")
set(CPACK_PACKAGE_NAME    "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "haomingbai@hotmail.com")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# Component behaviour: create one package per component (recommended)
# ONE_PER_GROUP will create one package per component (no grouping).
set(CPACK_COMPONENTS_GROUPING "ONE_PER_GROUP")

# Enable component-aware packaging for DEB and RPM
set(CPACK_DEB_COMPONENT_INSTALL ON)   # DEB: produce multiple .deb (e.g. package and package-dev)
set(CPACK_RPM_COMPONENT_INSTALL ON)   # RPM: produce multiple .rpm (e.g. package and package-devel)

# Which components we define (order is presentation order)
# Note: install(... COMPONENT ...) should use these names ("runtime", "devel")
if(BUILD_SHARED_LIBS)
  set(CPACK_COMPONENTS_ALL runtime devel)
else()
  # Static-only builds don't install runtime shared objects.
  set(CPACK_COMPONENTS_ALL devel)
endif()

# Human-friendly names & descriptions shown by some installers
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Runtime libraries")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "Shared libraries required to run ${PROJECT_NAME}.")
set(CPACK_COMPONENT_DEVEL_DISPLAY_NAME "Development files")
set(CPACK_COMPONENT_DEVEL_DESCRIPTION "Headers, CMake config and pkg-config files for developing against ${PROJECT_NAME}.")

# Let dpkg-shlibdeps compute runtime deps automatically for DEB
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Bind shlibdeps specifically to the runtime component (so found deps go into the runtime package)
set(CPACK_DEBIAN_RUNTIME_PACKAGE_SHLIBDEPS ON)

# Per-component package names (upper-case component id in variable name)
# Debian naming: we want e.g. bsrvcore_<ver> and bsrvcore-dev_<ver>
set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_DEBIAN_DEVEL_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-dev")

# Ensure devel package metadata pulls required development dependencies.
# - Public headers expose Boost.Asio/Beast and OpenSSL types.
# - Linking against bsrvcore also needs Boost.System and Boost.URL dev files.
if(BUILD_SHARED_LIBS)
  set(CPACK_DEBIAN_DEVEL_PACKAGE_DEPENDS
    "${CPACK_DEBIAN_RUNTIME_PACKAGE_NAME} (= ${CPACK_PACKAGE_VERSION}), libboost-dev, libboost-system-dev, libboost-url-dev, libssl-dev"
  )
else()
  set(CPACK_DEBIAN_DEVEL_PACKAGE_DEPENDS
    "libboost-dev, libboost-system-dev, libboost-url-dev, libssl-dev"
  )
endif()

# RPM naming: typical rpm convention uses "-devel"
set(CPACK_RPM_RUNTIME_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_RPM_DEVEL_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-devel")

# Ensure devel RPM declares toolchain-level development dependencies.
# boost-devel provides the public Boost.Asio/Beast headers and component libs.
if(BUILD_SHARED_LIBS)
  set(CPACK_RPM_DEVEL_PACKAGE_REQUIRES "${CPACK_RPM_RUNTIME_PACKAGE_NAME} = ${CPACK_PACKAGE_VERSION}, boost-devel, openssl-devel")
else()
  set(CPACK_RPM_DEVEL_PACKAGE_REQUIRES "boost-devel, openssl-devel")
endif()

# Refresh linker cache on install/uninstall so newly installed libbsrvcore.so.N
# can be found immediately by runtime loaders.
set(CPACK_RPM_RUNTIME_POST_INSTALL_SCRIPT_FILE
  "${CMAKE_SOURCE_DIR}/cmake/cpack/rpm/runtime-post.sh"
)
set(CPACK_RPM_RUNTIME_POST_UNINSTALL_SCRIPT_FILE
  "${CMAKE_SOURCE_DIR}/cmake/cpack/rpm/runtime-postun.sh"
)

# Optional: short summaries for each RPM package
set(CPACK_RPM_RUNTIME_PACKAGE_SUMMARY "Runtime libraries for ${PROJECT_NAME}")
set(CPACK_RPM_DEVEL_PACKAGE_SUMMARY "Development files (headers & cmake config) for ${PROJECT_NAME}")

# If you need to set Debian package architecture explicitly:
# set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")

# Resource files
if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
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
