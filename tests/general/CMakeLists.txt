add_subdirectory(core)
add_subdirectory(route)
add_subdirectory(session)
add_subdirectory(connection)
add_subdirectory(bsrvrun)

if(TARGET bsrvcore_c)
  add_subdirectory(c_binding)
endif()
