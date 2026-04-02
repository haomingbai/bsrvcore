add_library(bsrvcore_bsrvrun_common_obj OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/string.cc
)
target_link_libraries(bsrvcore_bsrvrun_common_obj PRIVATE
  bsrvcore_public_api
)
set_target_properties(bsrvcore_bsrvrun_common_obj PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

set(BSRVCORE_OBJECT_TARGETS ${BSRVCORE_OBJECT_TARGETS}
    bsrvcore_bsrvrun_common_obj PARENT_SCOPE)

option(BSRVCORE_PREFER_STATIC_YAML_CPP
  "Prefer a static yaml-cpp when building bsrvrun; fall back to vendored static yaml-cpp if needed."
  ON
)

function(bsrvcore_resolve_yaml_cpp_target out_var)
  if(BSRVCORE_PREFER_STATIC_YAML_CPP)
    find_path(BSRVCORE_YAML_CPP_INCLUDE_DIR NAMES yaml-cpp/yaml.h)

    set(_saved_find_library_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(WIN32)
      set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a)
    else()
      set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
    find_library(BSRVCORE_YAML_CPP_STATIC_LIBRARY NAMES yaml-cpp)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_saved_find_library_suffixes})

    if(BSRVCORE_YAML_CPP_STATIC_LIBRARY AND BSRVCORE_YAML_CPP_INCLUDE_DIR)
      add_library(bsrvcore_yaml_cpp_static STATIC IMPORTED GLOBAL)
      set_target_properties(bsrvcore_yaml_cpp_static PROPERTIES
        IMPORTED_LOCATION "${BSRVCORE_YAML_CPP_STATIC_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${BSRVCORE_YAML_CPP_INCLUDE_DIR}"
      )
      set(${out_var} bsrvcore_yaml_cpp_static PARENT_SCOPE)
      return()
    endif()

    message(STATUS "Static yaml-cpp not found in system paths; using vendored static yaml-cpp for bsrvrun.")
  else()
    find_package(yaml-cpp QUIET CONFIG)

    foreach(_candidate IN ITEMS yaml-cpp::yaml-cpp yaml-cpp)
      if(TARGET ${_candidate})
        set(${out_var} ${_candidate} PARENT_SCOPE)
        return()
      endif()
    endforeach()

    message(STATUS "yaml-cpp not found; using vendored static yaml-cpp for bsrvrun.")
  endif()

  # yaml-cpp upstream releases may still declare old CMake minima; this keeps
  # configure compatible on newer CMake versions that removed old policy ranges.
  if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  endif()

  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
  set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.9.0
  )
  FetchContent_MakeAvailable(yaml-cpp)

  if(TARGET yaml-cpp::yaml-cpp)
    set(${out_var} yaml-cpp::yaml-cpp PARENT_SCOPE)
  elseif(TARGET yaml-cpp)
    set(${out_var} yaml-cpp PARENT_SCOPE)
  else()
    message(FATAL_ERROR "yaml-cpp target was not created by the vendored build.")
  endif()
endfunction()

bsrvcore_resolve_yaml_cpp_target(BSRVCORE_YAML_CPP_TARGET)

add_library(
  bsrvrun_runtime
  ${CMAKE_CURRENT_SOURCE_DIR}/config_loader_parsing.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/config_loader_loading.cc
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
  PRIVATE
    bsrvcore_route_internal_api
    ${BSRVCORE_YAML_CPP_TARGET}
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
