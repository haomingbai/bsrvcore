add_library(bsrvcore_route_internal_api INTERFACE)
target_link_libraries(bsrvcore_route_internal_api INTERFACE bsrvcore_public_api)
target_include_directories(bsrvcore_route_internal_api INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include_exported
)

add_library(bsrvcore_route_obj OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/computing_route_handler.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/empty_route_handler.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_route_table_layer.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_route_table_match.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_route_table_registry.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_route_table_mount.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/impl/http_route_target_validator.cc
)
target_link_libraries(bsrvcore_route_obj PRIVATE bsrvcore_route_internal_api)
target_include_directories(bsrvcore_route_obj PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(bsrvcore_route_obj PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

set(BSRVCORE_OBJECT_TARGETS ${BSRVCORE_OBJECT_TARGETS} bsrvcore_route_obj
    PARENT_SCOPE)
