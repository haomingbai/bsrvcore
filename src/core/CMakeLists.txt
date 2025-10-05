set(CORE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_accept.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_config.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_construct.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_runtime.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_util.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/empty_logger.cc
)

set(CORE_SOURCES ${CORE_SOURCES} PARENT_SCOPE)
