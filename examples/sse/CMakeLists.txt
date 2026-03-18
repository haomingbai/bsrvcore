add_executable(example_sse_stream stream.cc)
target_link_libraries(example_sse_stream PRIVATE bsrvcore)
set_target_properties(example_sse_stream PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
)
