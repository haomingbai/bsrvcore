set(CONNECTION_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_client_task.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_client_task_flow.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_client_task_callbacks.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_client_session.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_client_session_cookie.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_sse_client_task.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_sse_client_task_startup.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_sse_client_task_stream.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/sse_event_parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/impl/http_url_parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/multipart_parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/put_processor.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/http_server_connection.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/http_server_task_base.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/http_server_task_lifecycle.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/server_set_cookie.cc
)

set(CONNECTION_SOURCES ${CONNECTION_SOURCES} PARENT_SCOPE)
