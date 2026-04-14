set(BSRVCORE_C_INSTALL_CONFIGDIR "${CMAKE_INSTALL_LIBDIR}/cmake/bsrvcore_c")

target_sources(bsrvcore PRIVATE
  callback_adapters.cc
  server.cc
  task.cc
)
target_include_directories(bsrvcore PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Build-tree convenience target for local examples/tests. The installed C
# package is generated manually so it does not depend on the C++ devel package.
add_library(bsrvcore_c INTERFACE)
add_library(bsrvcore::bsrvcore_c ALIAS bsrvcore_c)
target_link_libraries(bsrvcore_c INTERFACE bsrvcore)
target_include_directories(bsrvcore_c INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/bsrvcore-c/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bsrvcore-c
  COMPONENT c_devel
  FILES_MATCHING PATTERN "*.h*"
)

set(BSRVCORE_C_PC_REQUIRES_PRIVATE_LINE "")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/bsrvcore-c.pc.in
               ${CMAKE_CURRENT_BINARY_DIR}/bsrvcore-c.pc @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bsrvcore-c.pc
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig
  COMPONENT c_devel
)

configure_package_config_file(
  ${PROJECT_SOURCE_DIR}/cmake/bsrvcore_cConfig.cmake.in
  ${PROJECT_BINARY_DIR}/bsrvcore_cConfig.cmake
  INSTALL_DESTINATION ${BSRVCORE_C_INSTALL_CONFIGDIR}
)

write_basic_package_version_file(
  ${PROJECT_BINARY_DIR}/bsrvcore_cConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

install(FILES
  ${PROJECT_BINARY_DIR}/bsrvcore_cConfig.cmake
  ${PROJECT_BINARY_DIR}/bsrvcore_cConfigVersion.cmake
  DESTINATION ${BSRVCORE_C_INSTALL_CONFIGDIR}
  COMPONENT c_devel
)
