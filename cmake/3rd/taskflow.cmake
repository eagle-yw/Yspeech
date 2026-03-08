set(TASKFLOW_ARCHIVE "${ARCHIVE_DIR}/taskflow-4.0.0.tar.gz" CACHE FILEPATH "Taskflow 4.0.0 archive path")
if(NOT EXISTS "${TASKFLOW_ARCHIVE}")
  message(FATAL_ERROR "Taskflow archive not found: ${TASKFLOW_ARCHIVE}")
endif()

set(PREBUILT_TASKFLOW_DIR "${LIB_DIR}/include")
if(EXISTS "${PREBUILT_TASKFLOW_DIR}/taskflow/taskflow.hpp")
  add_library(taskflow INTERFACE)
  target_include_directories(taskflow INTERFACE "${PREBUILT_TASKFLOW_DIR}")
  add_library(Taskflow ALIAS taskflow)
  set(taskflow_SOURCE_DIR "${PREBUILT_TASKFLOW_DIR}" CACHE INTERNAL "Taskflow source directory")
  message(STATUS "Using prebuilt Taskflow from ${PREBUILT_TASKFLOW_DIR}")
  return()
endif()

message(STATUS "Building Taskflow from source...")

set(TF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TF_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(TF_BUILD_CUDA OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  taskflow
  URL "${TASKFLOW_ARCHIVE}"
  URL_HASH SHA256=a9d27ad29caffc95e394976c6a362debb94194f9b3fbb7f25e34aaf54272f497
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(taskflow)
yspeech_set_folder_for_targets_in_dir(${taskflow_SOURCE_DIR} "3rdparty")

FetchContent_GetProperties(taskflow)

set(TRAITS_HPP "${taskflow_SOURCE_DIR}/taskflow/utility/traits.hpp")
if(EXISTS "${TRAITS_HPP}")
  file(READ "${TRAITS_HPP}" TRAITS_CONTENT)
  if(NOT TRAITS_CONTENT MATCHES "#include <bit>")
    string(REPLACE 
      "#if __has_include(<latch>)\n#include <latch>\n#endif" 
      "#if __has_include(<latch>)\n#include <latch>\n#endif\n\n#if __has_include(<bit>)\n#include <bit>\n#endif" 
      TRAITS_CONTENT "${TRAITS_CONTENT}")
    file(WRITE "${TRAITS_HPP}" "${TRAITS_CONTENT}")
    message(STATUS "Patched taskflow/utility/traits.hpp for C++23 compatibility")
  endif()
endif()

if(NOT TARGET taskflow)
  add_library(taskflow INTERFACE)
  target_include_directories(taskflow INTERFACE "${taskflow_SOURCE_DIR}")
endif()

if(YSPEECH_BUILD_DEPS)
  add_custom_target(install_taskflow
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/include"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${taskflow_SOURCE_DIR}/taskflow" "${LIB_DIR}/include/taskflow"
    COMMENT "Installing Taskflow to ${LIB_DIR}"
  )
  set_target_properties(install_taskflow PROPERTIES FOLDER "CMake/Install")
  add_dependencies(yspeech_install_3rd_lib install_taskflow)
endif()
