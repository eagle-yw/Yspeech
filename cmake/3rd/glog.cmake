set(GLOG_ARCHIVE "${ARCHIVE_DIR}/glog-0.7.1.tar.gz" CACHE FILEPATH "glog 0.7.1 archive path")
if(NOT EXISTS "${GLOG_ARCHIVE}")
  message(FATAL_ERROR "glog archive not found: ${GLOG_ARCHIVE}")
endif()

set(PREBUILT_GLOG_LIB "${LIB_DIR}/lib/libglog.a")
set(PREBUILT_GLOG_EXPORT "${LIB_DIR}/include/glog/export.h")

if(EXISTS "${PREBUILT_GLOG_LIB}" AND EXISTS "${PREBUILT_GLOG_EXPORT}")
  add_library(glog STATIC IMPORTED)
  set_target_properties(glog PROPERTIES 
    IMPORTED_LOCATION "${PREBUILT_GLOG_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIB_DIR}/include"
  )
  add_library(glog::glog ALIAS glog)
  message(STATUS "Using prebuilt glog from ${LIB_DIR}")
  return()
endif()

message(STATUS "Building glog from source...")

set(WITH_GFLAGS OFF CACHE BOOL "Disable gflags for glog" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

FetchContent_Declare(
  glog
  URL "${GLOG_ARCHIVE}"
  URL_HASH SHA256=00e4a87e87b7e7612f519a41e491f16623b12423620006f59f5688bfd8d13b08
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(glog)
yspeech_set_folder_for_targets_in_dir(${glog_SOURCE_DIR} "3rdparty")

if(YSPEECH_BUILD_DEPS)
  add_custom_target(install_glog
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/lib"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/include/glog"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:glog>" "${LIB_DIR}/lib/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${glog_BINARY_DIR}/glog" "${LIB_DIR}/include/glog"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${glog_SOURCE_DIR}/src/glog" "${LIB_DIR}/include/glog"
    COMMENT "Installing glog to ${LIB_DIR}"
  )
  set_target_properties(install_glog PROPERTIES FOLDER "CMake/Install")
  add_dependencies(yspeech_install_3rd_lib install_glog)
  add_dependencies(install_glog glog)
endif()
