set(BSRVCORE_C_INSTALL_CONFIGDIR "${CMAKE_INSTALL_LIBDIR}/cmake/bsrvcore_c")

add_library(bsrvcore_c
  callback_adapters.cc
  server.cc
  task.cc
)
add_library(bsrvcore::bsrvcore_c ALIAS bsrvcore_c)

target_compile_features(bsrvcore_c PUBLIC cxx_std_20)
target_link_libraries(bsrvcore_c PRIVATE bsrvcore)
target_include_directories(bsrvcore_c
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(bsrvcore_c PROPERTIES
  EXPORT_NAME bsrvcore_c
  OUTPUT_NAME bsrvcore-c
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)

if(NOT WIN32)
  set_target_properties(bsrvcore_c PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

install(TARGETS bsrvcore_c
  EXPORT bsrvcoreCTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT c_devel
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/bsrvcore-c/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bsrvcore-c
  COMPONENT c_devel
  FILES_MATCHING PATTERN "*.h*"
)

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

install(EXPORT bsrvcoreCTargets
  FILE bsrvcore_cTargets.cmake
  NAMESPACE bsrvcore::
  DESTINATION ${BSRVCORE_C_INSTALL_CONFIGDIR}
  COMPONENT c_devel
)

export(EXPORT bsrvcoreCTargets
  FILE "${PROJECT_BINARY_DIR}/bsrvcore_cTargets.cmake"
  NAMESPACE bsrvcore::
)
