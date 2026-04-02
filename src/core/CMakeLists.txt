add_library(bsrvcore_core_obj OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/blue_print.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_accept.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_route_config.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_runtime_config.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_construct.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_runtime.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_util.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/empty_logger.cc
)
target_link_libraries(bsrvcore_core_obj PRIVATE
  bsrvcore_public_api
  bsrvcore_connection_internal_api
  bsrvcore_route_internal_api
  bsrvcore_session_internal_api
  bthpool::headers
)
target_include_directories(bsrvcore_core_obj PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(bsrvcore_core_obj PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

set(BSRVCORE_OBJECT_TARGETS ${BSRVCORE_OBJECT_TARGETS} bsrvcore_core_obj
    PARENT_SCOPE)
