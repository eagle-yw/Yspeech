set(GTEST_ARCHIVE "${ARCHIVE_DIR}/googletest-1.17.0.tar.gz" CACHE FILEPATH "GoogleTest 1.17.0 archive path")
if(NOT EXISTS "${GTEST_ARCHIVE}")
  message(FATAL_ERROR "GoogleTest archive not found: ${GTEST_ARCHIVE}")
endif()

set(PREBUILT_GTEST_LIB "${LIB_DIR}/lib/libgtest.a")
set(PREBUILT_GTEST_MAIN_LIB "${LIB_DIR}/lib/libgtest_main.a")
set(PREBUILT_GMOCK_LIB "${LIB_DIR}/lib/libgmock.a")
set(PREBUILT_GMOCK_MAIN_LIB "${LIB_DIR}/lib/libgmock_main.a")

if(EXISTS "${PREBUILT_GTEST_LIB}" AND EXISTS "${PREBUILT_GTEST_MAIN_LIB}" AND EXISTS "${PREBUILT_GMOCK_LIB}" AND EXISTS "${PREBUILT_GMOCK_MAIN_LIB}")
  add_library(gtest STATIC IMPORTED)
  set_target_properties(gtest PROPERTIES 
    IMPORTED_LOCATION "${PREBUILT_GTEST_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIB_DIR}/include"
  )
  
  add_library(gtest_main STATIC IMPORTED)
  set_target_properties(gtest_main PROPERTIES 
    IMPORTED_LOCATION "${PREBUILT_GTEST_MAIN_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIB_DIR}/include"
    INTERFACE_LINK_LIBRARIES gtest
  )
  
  add_library(gmock STATIC IMPORTED)
  set_target_properties(gmock PROPERTIES 
    IMPORTED_LOCATION "${PREBUILT_GMOCK_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIB_DIR}/include"
    INTERFACE_LINK_LIBRARIES gtest
  )
  
  add_library(gmock_main STATIC IMPORTED)
  set_target_properties(gmock_main PROPERTIES 
    IMPORTED_LOCATION "${PREBUILT_GMOCK_MAIN_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIB_DIR}/include"
    INTERFACE_LINK_LIBRARIES gmock
  )
  
  add_library(GTest::gtest ALIAS gtest)
  add_library(GTest::gtest_main ALIAS gtest_main)
  
  message(STATUS "Using prebuilt GoogleTest from ${LIB_DIR}")
  return()
endif()

message(STATUS "Building GoogleTest from source...")

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

FetchContent_Declare(
  googletest
  URL "${GTEST_ARCHIVE}"
  URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(googletest)
yspeech_set_folder_for_targets_in_dir(${googletest_SOURCE_DIR} "3rdparty")

if(YSPEECH_BUILD_DEPS)
  add_custom_target(install_gtest
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/lib"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/include"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:gtest>" "${LIB_DIR}/lib/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:gtest_main>" "${LIB_DIR}/lib/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:gmock>" "${LIB_DIR}/lib/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:gmock_main>" "${LIB_DIR}/lib/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${googletest_SOURCE_DIR}/googletest/include/gtest" "${LIB_DIR}/include/gtest"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${googletest_SOURCE_DIR}/googlemock/include/gmock" "${LIB_DIR}/include/gmock"
    COMMENT "Installing GoogleTest to ${LIB_DIR}"
  )
  set_target_properties(install_gtest PROPERTIES FOLDER "CMake/Install")
  add_dependencies(yspeech_install_3rd_lib install_gtest)
  add_dependencies(install_gtest gtest gtest_main gmock gmock_main)
endif()
