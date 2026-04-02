add_library(bsrvcore_session_internal_api INTERFACE)
target_link_libraries(bsrvcore_session_internal_api INTERFACE
  bsrvcore_public_api
)
target_include_directories(bsrvcore_session_internal_api INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include_exported
)

add_library(bsrvcore_session_obj OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/session_context_entry.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/session_key_heap_entry.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/session_map.cc  
  ${CMAKE_CURRENT_SOURCE_DIR}/session_map_cleaner.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/context.cc
)
target_link_libraries(bsrvcore_session_obj PRIVATE
  bsrvcore_session_internal_api
)
target_include_directories(bsrvcore_session_obj PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(bsrvcore_session_obj PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

set(BSRVCORE_OBJECT_TARGETS ${BSRVCORE_OBJECT_TARGETS} bsrvcore_session_obj
    PARENT_SCOPE)
