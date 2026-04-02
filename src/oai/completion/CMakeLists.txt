add_library(bsrvcore_oai_obj OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/oai_completion.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/oai_completion_request.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/oai_completion_response.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/oai_completion_factory.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/oai_completion_state.cc
)
target_link_libraries(bsrvcore_oai_obj PRIVATE bsrvcore_public_api)
set_target_properties(bsrvcore_oai_obj PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

set(BSRVCORE_OBJECT_TARGETS ${BSRVCORE_OBJECT_TARGETS} bsrvcore_oai_obj
    PARENT_SCOPE)
