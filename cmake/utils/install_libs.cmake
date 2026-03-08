cmake_minimum_required(VERSION 3.30)

file(GLOB_RECURSE LIB_FILES "${CACHE_DIR}/*.a")
foreach(LIB_FILE ${LIB_FILES})
  get_filename_component(LIB_NAME "${LIB_FILE}" NAME)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIB_FILE}" "${LIB_DIR}/${LIB_NAME}"
    RESULT_VARIABLE result
  )
  if(result EQUAL 0)
    message(STATUS "Installed: ${LIB_NAME}")
  endif()
endforeach()
