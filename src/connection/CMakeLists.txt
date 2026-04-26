add_library(bsrvcore_connection_internal_api INTERFACE)
target_link_libraries(bsrvcore_connection_internal_api INTERFACE
  bsrvcore_public_api
)
target_include_directories(bsrvcore_connection_internal_api INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include_exported
)

add_library(bsrvcore_connection_obj OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_client_task.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_client_task_flow.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_client_task_callbacks.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_client_session.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/stream_builder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/request_assembler.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_sse_client_task.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_sse_client_task_startup.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/http_sse_client_task_stream.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/websocket_client_task.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/websocket_client_task_startup.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/websocket_client_task_stream.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/multipart_generator.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/put_generator.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/sse_event_parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/impl/default_client_ssl_context.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/client/impl/http_url_parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/multipart_parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/put_processor.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/http_server_connection.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/http_server_task_base.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/http_server_task_lifecycle.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/server_set_cookie.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/server/websocket_server_task.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/websocket/websocket_task_base.cc
)
target_link_libraries(bsrvcore_connection_obj PRIVATE
  bsrvcore_connection_internal_api
)
target_include_directories(bsrvcore_connection_obj PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(bsrvcore_connection_obj PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

set(BSRVCORE_OBJECT_TARGETS ${BSRVCORE_OBJECT_TARGETS}
    bsrvcore_connection_obj PARENT_SCOPE)
