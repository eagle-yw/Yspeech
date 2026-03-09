set(SPDLOG_ARCHIVE "${ARCHIVE_DIR}/spdlog-1.17.0.tar.gz" CACHE FILEPATH "spdlog 1.17.0 archive path")
if(NOT EXISTS "${SPDLOG_ARCHIVE}")
  message(FATAL_ERROR "spdlog archive not found: ${SPDLOG_ARCHIVE}")
endif()

set(PREBUILT_SPDLOG_DIR "${LIB_DIR}/include")
if(EXISTS "${PREBUILT_SPDLOG_DIR}/spdlog/spdlog.h")
  add_library(spdlog INTERFACE)
  target_include_directories(spdlog INTERFACE "${PREBUILT_SPDLOG_DIR}")
  add_library(spdlog::spdlog ALIAS spdlog)
  message(STATUS "Using prebuilt spdlog from ${PREBUILT_SPDLOG_DIR}")
  return()
endif()

message(STATUS "Building spdlog from source...")

set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  spdlog
  URL "${SPDLOG_ARCHIVE}"
  URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(spdlog)
yspeech_set_folder_for_targets_in_dir(${spdlog_SOURCE_DIR} "3rdparty")

if(YSPEECH_BUILD_DEPS)
  add_custom_target(install_spdlog
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/include"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${spdlog_SOURCE_DIR}/include/spdlog" "${LIB_DIR}/include/spdlog"
    COMMENT "Installing spdlog to ${LIB_DIR}"
  )
  set_target_properties(install_spdlog PROPERTIES FOLDER "CMake/Install")
  add_dependencies(yspeech_install_3rd_lib install_spdlog)
endif()
