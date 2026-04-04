add_library(bsrvcore_file_obj OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/file_reader.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/file_writer.cc
)
target_link_libraries(bsrvcore_file_obj PRIVATE
  bsrvcore_public_api
)
set_target_properties(bsrvcore_file_obj PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

set(BSRVCORE_OBJECT_TARGETS ${BSRVCORE_OBJECT_TARGETS}
    bsrvcore_file_obj PARENT_SCOPE)
