set(CONNECTION_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_connection.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_server_task.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_client_task.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_client_session.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/http_sse_client_task.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/sse_event_parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server_set_cookie.cc
)

set(CONNECTION_SOURCES ${CONNECTION_SOURCES} PARENT_SCOPE)
