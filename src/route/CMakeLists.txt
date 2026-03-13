set(ROUTE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/empty_route_handler.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_route_table_layer.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_route_table.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/impl/http_route_target_validator.cc
)

set(ROUTE_SOURCES ${ROUTE_SOURCES} PARENT_SCOPE)
