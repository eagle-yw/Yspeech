cmake_minimum_required(VERSION 3.30 FATAL_ERROR)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")

include(cmake/toolchain/clang-toolchain.cmake)

project(Yspeech VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-deprecated-declarations)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_SCAN_FOR_MODULES ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
include(cmake/utils/set_folder.cmake)

set(ARCHIVE_DIR "${CMAKE_SOURCE_DIR}/3rd-archive" CACHE PATH "Third party archives directory")

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/3rd-lib")
    set(YSPEECH_BUILD_DEPS ON CACHE BOOL "Build 3rd party dependencies from source" FORCE)
    message(STATUS "3rd-lib not found, auto-enabling YSPEECH_BUILD_DEPS=ON")
else()
    option(YSPEECH_BUILD_DEPS "Build 3rd party dependencies from source and install to 3rd-lib" OFF)
endif()

if(YSPEECH_BUILD_DEPS)
    set(LIB_DIR "${CMAKE_SOURCE_DIR}/3rd-lib" CACHE PATH "Third party libraries directory" FORCE)
else()
    set(LIB_DIR "${CMAKE_SOURCE_DIR}/3rd-lib" CACHE PATH "Third party libraries directory")
endif()

set(THIRD_PARTY_CACHE "${CMAKE_SOURCE_DIR}/.3rd-cache" CACHE PATH "Third party cache directory")
if(NOT EXISTS "${THIRD_PARTY_CACHE}")
    file(MAKE_DIRECTORY "${THIRD_PARTY_CACHE}")
endif()
set(FETCHCONTENT_BASE_DIR "${THIRD_PARTY_CACHE}")
message(STATUS "Using third-party cache at: ${FETCHCONTENT_BASE_DIR}")

include(FetchContent)

if(YSPEECH_BUILD_DEPS)
    add_custom_target(yspeech_install_3rd_lib ALL
        COMMENT "Installing 3rd party libraries to ${LIB_DIR}"
    )
    set_target_properties(yspeech_install_3rd_lib PROPERTIES FOLDER "CMake/Install")
endif()

include(${CMAKE_SOURCE_DIR}/cmake/3rd/json.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/3rd/gtest.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/3rd/taskflow.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/3rd/glog.cmake)

include(cmake/3rd/onnxruntime.cmake)

add_subdirectory(src)
add_subdirectory(test)
