set(BSRVRUN_COMMON_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/string.cc
)
set(BSRVRUN_COMMON_SOURCES ${BSRVRUN_COMMON_SOURCES} PARENT_SCOPE)

find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
  )
  FetchContent_MakeAvailable(yaml-cpp)
endif()

add_library(
  bsrvrun_runtime
  ${CMAKE_CURRENT_SOURCE_DIR}/config_loader.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parameter_map_impl.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin_loader.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server_builder.cc
)

target_include_directories(bsrvrun_runtime
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bsrvrun_runtime
  PUBLIC
    bsrvcore
    yaml-cpp
)

if(CMAKE_DL_LIBS)
  target_link_libraries(bsrvrun_runtime PUBLIC ${CMAKE_DL_LIBS})
endif()

if(BSRVCORE_BUILD_TOOLS)
  add_executable(
    bsrvrun
    ${CMAKE_CURRENT_SOURCE_DIR}/bsrvrun_main.cc
  )

  target_link_libraries(bsrvrun PRIVATE bsrvrun_runtime Boost::program_options)

  install(TARGETS bsrvrun
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )
endif()
