# Minimal src CMakeLists for bsrvcore

# Add the subdirectories in this project.
add_subdirectory(route)
add_subdirectory(session)

# List source files explicitly for better incremental builds

set(BSRVCORE_SOURCES
  ${ROUTE_SOURCES}
  ${SESSION_SOURCES}
)

# Create library (static or shared follows BUILD_SHARED_LIBS)
add_library(bsrvcore SHARED ${BSRVCORE_SOURCES})

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

# Install the target and export CMake targets file
install(TARGETS bsrvcore
  EXPORT bsrvcoreTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT bsrvcoreTargets
  FILE bsrvcoreTargets.cmake
  NAMESPACE bsrvcore::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bsrvcore
)

