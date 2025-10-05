cmake_minimum_required(VERSION 3.25)
project(bsrvcore VERSION 0.0.1 LANGUAGES CXX)

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

set(CPACK_GENERATOR "DEB;RPM;TGZ")
set(CPACK_PACKAGE_NAME    "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "email@example.com")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# Let dpkg-shlibdeps compute runtime deps automatically (recommended)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# If you must add manual runtime deps, avoid "-dev" here:
# set(CPACK_DEBIAN_PACKAGE_DEPENDS "libboost-system1.75.0") 

# RPM: runtime requires (do not list -devel here)
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP   "Development/Libraries")
set(CPACK_RPM_PACKAGE_REQUIRES "boost")  # adjust per-distro if needed

if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif()

include(CPack)

