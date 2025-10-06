cmake_minimum_required(VERSION 3.25)
project(bsrvcore VERSION 0.0.2 LANGUAGES CXX)

# Use C++20
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Generate compile_commands.json to help LSP server
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Require Boost.Beast and Boost.Asio (header-only; Boost.System required, Boost.url required)
find_package(Boost 1.75 COMPONENTS system url CONFIG REQUIRED)

# Standard install directories
include(GNUInstallDirs)

# Helpers to generate package config files
include(CMakePackageConfigHelpers)

# Build library from src
add_subdirectory(src)

# Install public headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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
set(CPACK_COMPONENTS_ALL runtime devel)

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

# RPM naming: typical rpm convention uses "-devel"
set(CPACK_RPM_RUNTIME_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_RPM_DEVEL_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-devel")

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
