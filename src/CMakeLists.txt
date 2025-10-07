# Minimal src CMakeLists for bsrvcore (component-aware installs)
# - splits installed files into two CPack components: "runtime" and "devel"
# - runtime: shared libraries, runtime executables
# - devel: headers, static libs, CMake export/config files

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

# Add the subdirectories in this project.
add_subdirectory(route)
add_subdirectory(session)
add_subdirectory(connection)
add_subdirectory(core)

# List source files explicitly for better incremental builds
set(BSRVCORE_SOURCES
  ${ROUTE_SOURCES}
  ${SESSION_SOURCES}
  ${CONNECTION_SOURCES}
  ${CORE_SOURCES}
)

# Create library (static or shared follows BUILD_SHARED_LIBS)
add_library(bsrvcore SHARED ${BSRVCORE_SOURCES})

# Link libraries
target_link_libraries(bsrvcore
    PRIVATE 
        Boost::system
        Boost::url
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Public include directories for build and install
target_include_directories(bsrvcore
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>  # public headers for build
    $<INSTALL_INTERFACE:include>                                # public headers for install
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>      # private headers for build
)

# Set version and export name
set_target_properties(bsrvcore PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME bsrvcore
)

# Position independent code for non-Windows
if(NOT WIN32)
  set_target_properties(bsrvcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
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
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT devel
)

# Install public headers into devel component
# Use top-level source include directory so this works when invoked from top-level build
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  COMPONENT devel
  FILES_MATCHING PATTERN "*.h*"
)

# ------------------------ pkg-config generation ------------------------
# Generate a pkg-config file for bsrvcore and install it with the devel component.
# We avoid requiring a source-level .pc.in by generating the template at configure time.
if(NOT DEFINED PROJECT_DESCRIPTION)
  set(PROJECT_DESCRIPTION "${PROJECT_NAME} library")
endif()

# Write a small .pc.in template into the build tree, then configure it.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bsrvcore.pc.in "prefix=@CMAKE_INSTALL_PREFIX@\nexec_prefix=\\${prefix}\nlibdir=\\${exec_prefix}/@CMAKE_INSTALL_LIBDIR@\nincludedir=\\${prefix}/@CMAKE_INSTALL_INCLUDEDIR@\n\nName: ${PROJECT_NAME}\nDescription: ${PROJECT_DESCRIPTION}\nVersion: ${PROJECT_VERSION}\nLibs: -L\\${libdir} -l${PROJECT_NAME}\nCflags: -I\\${includedir}\n")

configure_file(${CMAKE_CURRENT_BINARY_DIR}/bsrvcore.pc.in
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

# ------------------------------------------------------------------
# Optional: generate a pkg-config file at install time (uncomment if needed)
# configure_file(${CMAKE_CURRENT_SOURCE_DIR}/pkgconfig/bsrvcore.pc.in
#                ${CMAKE_CURRENT_BINARY_DIR}/bsrvcore.pc @ONLY)
# install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bsrvcore.pc
#   DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig COMPONENT devel)
# ------------------------------------------------------------------

