set(ONNXRUNTIME_ARCHIVE "${ARCHIVE_DIR}/onnxruntime-1.24.3.tar.gz" CACHE FILEPATH "ONNX Runtime 1.24.3 archive path")
option(YSPEECH_FORCE_REBUILD_ONNXRUNTIME "Force rebuilding ONNX Runtime from source" OFF)
if(NOT EXISTS "${ONNXRUNTIME_ARCHIVE}")
  message(FATAL_ERROR "ONNX Runtime archive not found: ${ONNXRUNTIME_ARCHIVE}")
endif()

set(PREBUILT_ONNXRUNTIME_LIB "${LIB_DIR}/lib/libonnxruntime_common.a")
set(CACHED_ONNXRUNTIME_LIB "${CMAKE_SOURCE_DIR}/.3rd-cache/onnxruntime-build/libonnxruntime_common.a")

if((EXISTS "${PREBUILT_ONNXRUNTIME_LIB}" OR EXISTS "${CACHED_ONNXRUNTIME_LIB}") AND NOT YSPEECH_FORCE_REBUILD_ONNXRUNTIME)
  if(EXISTS "${PREBUILT_ONNXRUNTIME_LIB}")
    set(ORT_COMMON_LIB "${PREBUILT_ONNXRUNTIME_LIB}")
    set(ORT_LIB_GLOB_DIR "${LIB_DIR}/lib")
    set(ORT_INCLUDE_DIR_LOCAL "${LIB_DIR}/include/onnxruntime")
    set(ORT_SOURCE_DIR_LOCAL "${LIB_DIR}")
    set(ORT_LOCATION_TAG "${LIB_DIR}")
  else()
    set(ORT_COMMON_LIB "${CACHED_ONNXRUNTIME_LIB}")
    set(ORT_LIB_GLOB_DIR "${CMAKE_SOURCE_DIR}/.3rd-cache/onnxruntime-build")
    set(ORT_INCLUDE_DIR_LOCAL "${CMAKE_SOURCE_DIR}/.3rd-cache/onnxruntime-src/include/onnxruntime")
    set(ORT_SOURCE_DIR_LOCAL "${CMAKE_SOURCE_DIR}/.3rd-cache/onnxruntime-src")
    set(ORT_LOCATION_TAG "${CMAKE_SOURCE_DIR}/.3rd-cache")
  endif()

  add_library(onnxruntime STATIC IMPORTED)
  set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ORT_COMMON_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${ORT_INCLUDE_DIR_LOCAL}"
  )
  
  file(GLOB ONNXRUNTIME_DEP_LIBS "${ORT_LIB_GLOB_DIR}/*.a")
  if(ORT_LOCATION_TAG STREQUAL "${CMAKE_SOURCE_DIR}/.3rd-cache")
    file(GLOB_RECURSE ONNXRUNTIME_3RDPARTY_LIBS "${CMAKE_SOURCE_DIR}/.3rd-cache/*-build/*.a")
    list(APPEND ONNXRUNTIME_DEP_LIBS ${ONNXRUNTIME_3RDPARTY_LIBS})
  endif()
  list(REMOVE_DUPLICATES ONNXRUNTIME_DEP_LIBS)
  list(REMOVE_ITEM ONNXRUNTIME_DEP_LIBS "${ORT_COMMON_LIB}")
  list(FILTER ONNXRUNTIME_DEP_LIBS EXCLUDE REGEX ".*/libgtest(_main)?\\.a$")
  list(FILTER ONNXRUNTIME_DEP_LIBS EXCLUDE REGEX ".*/libgmock(_main)?\\.a$")
  list(FILTER ONNXRUNTIME_DEP_LIBS EXCLUDE REGEX ".*/libkaldi-native-fbank-core\\.a$")
  if(ONNXRUNTIME_DEP_LIBS)
    set_target_properties(onnxruntime PROPERTIES
      INTERFACE_LINK_LIBRARIES "${ONNXRUNTIME_DEP_LIBS}"
    )
  endif()
  
  set(ONNXRUNTIME_LIBRARY onnxruntime CACHE INTERNAL "ONNX Runtime library")
  set(ONNXRUNTIME_INCLUDE_DIR "${ORT_INCLUDE_DIR_LOCAL}" CACHE INTERNAL "ONNX Runtime include directory")
  set(onnxruntime_SOURCE_DIR "${ORT_SOURCE_DIR_LOCAL}" CACHE INTERNAL "ONNX Runtime source directory")

  if(YSPEECH_BUILD_DEPS)
    if(ORT_LOCATION_TAG STREQUAL "${CMAKE_SOURCE_DIR}/.3rd-cache")
      set(ORT_INSTALL_CACHE_DIR "${CMAKE_SOURCE_DIR}/.3rd-cache")
    else()
      set(ORT_INSTALL_CACHE_DIR "${LIB_DIR}")
    endif()

    add_custom_target(install_onnxruntime
      COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/lib"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/include/onnxruntime"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${ORT_INCLUDE_DIR_LOCAL}" "${LIB_DIR}/include/onnxruntime"
      COMMAND ${CMAKE_COMMAND}
        -DCACHE_DIR="${ORT_INSTALL_CACHE_DIR}"
        -DLIB_DIR="${LIB_DIR}/lib"
        -P "${CMAKE_SOURCE_DIR}/cmake/utils/install_libs.cmake"
      COMMENT "Installing ONNX Runtime to ${LIB_DIR}"
    )
    set_target_properties(install_onnxruntime PROPERTIES FOLDER "CMake/Install")
    add_dependencies(yspeech_install_3rd_lib install_onnxruntime)
  endif()
  
  message(STATUS "Using prebuilt ONNX Runtime from ${ORT_LOCATION_TAG}")
  message(STATUS "  Library: ${ONNXRUNTIME_LIBRARY}")
  message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIR}")
  return()
endif()

message(STATUS "Building ONNX Runtime from source...")

set(DEPS_DIR "${ARCHIVE_DIR}/deps")
set(ONNXRUNTIME_DEPS_DIR "${DEPS_DIR}")

set(ONNX_DEPS
  "abseil-cpp-20250814.0.zip|https://github.com/abseil/abseil-cpp/archive/refs/tags/20250814.0.zip"
  "protobuf-v21.12.zip|https://github.com/protocolbuffers/protobuf/archive/refs/tags/v21.12.zip"
  "protoc-21.12-osx-universal_binary.zip|https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protoc-21.12-osx-universal_binary.zip"
  "re2-2024-07-02.zip|https://github.com/google/re2/archive/refs/tags/2024-07-02.zip"
  "flatbuffers-v23.5.26.zip|https://github.com/google/flatbuffers/archive/refs/tags/v23.5.26.zip"
  "onnx-v1.20.1.zip|https://github.com/onnx/onnx/archive/refs/tags/v1.20.1.zip"
  "eigen-3.4.zip|https://github.com/eigen-mirror/eigen/archive/1d8b82b0740839c0de7f1242a3585e3390ff5f33.zip"
  "json-v3.11.3.zip|https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip"
  "microsoft_gsl-v4.0.0.zip|https://github.com/microsoft/GSL/archive/refs/tags/v4.0.0.zip"
  "safeint-3.0.28.zip|https://github.com/dcleblanc/SafeInt/archive/refs/tags/3.0.28.zip"
  "date-v3.0.1.zip|https://github.com/HowardHinnant/date/archive/refs/tags/v3.0.1.zip"
  "mp11-boost-1.82.0.zip|https://github.com/boostorg/mp11/archive/refs/tags/boost-1.82.0.zip"
  "pytorch_cpuinfo-403d652.zip|https://github.com/pytorch/cpuinfo/archive/403d652dca4c1046e8145950b1c0997a9f748b57.zip"
  "coremltools-7.1.zip|https://github.com/apple/coremltools/archive/refs/tags/7.1.zip"
  "fp16-0a92994d729ff76a58f692d3028ca1b64b145d91.zip|https://github.com/Maratyszcza/FP16/archive/0a92994d729ff76a58f692d3028ca1b64b145d91.zip"
  "psimd-2024-07-02.zip|https://github.com/Maratyszcza/psimd/archive/072586a71b55b7f8c584153d223e95687148a900.zip"
)

file(MAKE_DIRECTORY "${DEPS_DIR}")

foreach(DEP_SPEC ${ONNX_DEPS})
  string(REPLACE "|" ";" DEP_LIST "${DEP_SPEC}")
  list(GET DEP_LIST 0 DEP_FILE)
  list(GET DEP_LIST 1 DEP_URL)
  
  set(DEP_PATH "${DEPS_DIR}/${DEP_FILE}")
  
  if(NOT EXISTS "${DEP_PATH}")
    message(STATUS "Downloading ${DEP_FILE}...")
    file(DOWNLOAD "${DEP_URL}" "${DEP_PATH}"
      SHOW_PROGRESS
      STATUS DOWNLOAD_STATUS
      TIMEOUT 300
    )
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
      message(FATAL_ERROR "Failed to download ${DEP_FILE} from ${DEP_URL}")
    endif()
  endif()
endforeach()

set(onnxruntime_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(onnxruntime_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(onnxruntime_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(onnxruntime_USE_CUDA OFF CACHE BOOL "" FORCE)
set(onnxruntime_USE_TENSORRT OFF CACHE BOOL "" FORCE)
set(onnxruntime_USE_OPENVINO OFF CACHE BOOL "" FORCE)
set(onnxruntime_USE_DNNL OFF CACHE BOOL "" FORCE)
if(APPLE)
  set(onnxruntime_USE_COREML ON CACHE BOOL "" FORCE)
else()
  set(onnxruntime_USE_COREML OFF CACHE BOOL "" FORCE)
endif()
set(onnxruntime_BUILD_FOR_NATIVE_MACHINE ON CACHE BOOL "" FORCE)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  set(onnxruntime_target_platform "arm64" CACHE STRING "Target platform" FORCE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
  set(onnxruntime_target_platform "x64" CACHE STRING "Target platform" FORCE)
else()
  set(onnxruntime_target_platform "${CMAKE_SYSTEM_PROCESSOR}" CACHE STRING "Target platform" FORCE)
endif()

set(CMAKE_CUDA_ARCHITECTURES "" CACHE STRING "Disable CUDA architectures" FORCE)

include(FetchContent)
FetchContent_Declare(
  onnxruntime
  URL "${ONNXRUNTIME_ARCHIVE}"
  URL_HASH SHA256=70364aa2cddfe535974bd160261c25743aca0ec3708770578813b0f9404bd064
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_GetProperties(onnxruntime)
if(NOT onnxruntime_POPULATED)
  cmake_policy(SET CMP0169 OLD)
  FetchContent_Populate(onnxruntime)
  
  set(CMAKE_POLICY_DEFAULT_CMP0169 OLD CACHE STRING "Policy CMP0169 for onnxruntime subdirectories" FORCE)
  
  function(install)
  endfunction()

  set(DEP_URL_abseil_cpp "file://${DEPS_DIR}/abseil-cpp-20250814.0.zip")
  set(DEP_URL_protobuf "file://${DEPS_DIR}/protobuf-v21.12.zip")
  set(DEP_URL_protoc_mac_universal "file://${DEPS_DIR}/protoc-21.12-osx-universal_binary.zip")
  set(DEP_URL_re2 "file://${DEPS_DIR}/re2-2024-07-02.zip")
  set(DEP_URL_flatbuffers "file://${DEPS_DIR}/flatbuffers-v23.5.26.zip")
  set(DEP_URL_onnx "file://${DEPS_DIR}/onnx-v1.20.1.zip")
  set(DEP_URL_eigen "file://${DEPS_DIR}/eigen-3.4.zip")
  set(DEP_URL_json "file://${DEPS_DIR}/json-v3.11.3.zip")
  set(DEP_URL_microsoft_gsl "file://${DEPS_DIR}/microsoft_gsl-v4.0.0.zip")
  set(DEP_URL_safeint "file://${DEPS_DIR}/safeint-3.0.28.zip")
  set(DEP_URL_date "file://${DEPS_DIR}/date-v3.0.1.zip")
  set(DEP_URL_mp11 "file://${DEPS_DIR}/mp11-boost-1.82.0.zip")
  set(DEP_URL_pytorch_cpuinfo "file://${DEPS_DIR}/pytorch_cpuinfo-403d652.zip")
  set(DEP_URL_psimd "file://${DEPS_DIR}/psimd-2024-07-02.zip")
  
  set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
  # CMake 4 drops compatibility with very old minimum versions used by psimd.
  # Keep policy floor at 3.5 so CoreML deps can still configure.
  set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)

  set(CMAKE_CXX_STANDARD_BACKUP ${CMAKE_CXX_STANDARD})
  set(CMAKE_CXX_STANDARD 17)

  set(onnxruntime_USE_PREBUILT_PROTOBUF OFF CACHE BOOL "" FORCE)
  set(onnxruntime_PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
  
  set(DEP_URL_protoc_linux_x64 "file://${DEPS_DIR}/dummy_protoc_linux.zip")
  
  set(DEPS_TXT_CONTENT 
"abseil_cpp;file://${DEPS_DIR}/abseil-cpp-20250814.0.zip;a9eb1d648cbca4d4d788737e971a6a7a63726b07
protobuf;file://${DEPS_DIR}/protobuf-v21.12.zip;7cf2733949036c7d52fda017badcab093fe73bfa
protoc_mac_universal;file://${DEPS_DIR}/protoc-21.12-osx-universal_binary.zip;23710c3d1c2036d8d65a6a22234372fa2d7af9ef
re2;file://${DEPS_DIR}/re2-2024-07-02.zip;646e1728269cde7fcef990bf4a8e87b047882e88
flatbuffers;file://${DEPS_DIR}/flatbuffers-v23.5.26.zip;59422c3b5e573dd192fead2834d25951f1c1670c
onnx;file://${DEPS_DIR}/onnx-v1.20.1.zip;30b80c81a1a381188896e86abe460c3c3f3091fd
eigen;file://${DEPS_DIR}/eigen-3.4.zip;05b19b49e6fbb91246be711d801160528c135e34
json;file://${DEPS_DIR}/json-v3.11.3.zip;5e88795165cc8590138d1f47ce94ee567b85b4d6
microsoft_gsl;file://${DEPS_DIR}/microsoft_gsl-v4.0.0.zip;cf368104cd22a87b4dd0c80228919bb2df3e2a14
safeint;file://${DEPS_DIR}/safeint-3.0.28.zip;23f252040ff6cb9f1fd18575b32fa8fb5928daac
date;file://${DEPS_DIR}/date-v3.0.1.zip;2dac0c81dc54ebdd8f8d073a75c053b04b56e159
mp11;file://${DEPS_DIR}/mp11-boost-1.82.0.zip;9bc9e01dffb64d9e0773b2e44d2f22c51aace063
pytorch_cpuinfo;file://${DEPS_DIR}/pytorch_cpuinfo-403d652.zip;30b2a07fe4bae8574f89176e56274cacdd6d135b
coremltools;file://${DEPS_DIR}/coremltools-7.1.zip;f1bab0f30966f2e217d8e01207d518f230a1641a
fp16;file://${DEPS_DIR}/fp16-0a92994d729ff76a58f692d3028ca1b64b145d91.zip;b985f6985a05a1c03ff1bb71190f66d8f98a1494
psimd;file://${DEPS_DIR}/psimd-2024-07-02.zip;1f5454b01f06f9656b77e4a5e2e31d7422487013
")
  
  if(EXISTS "${onnxruntime_SOURCE_DIR}/cmake/deps.txt")
    file(READ "${onnxruntime_SOURCE_DIR}/cmake/deps.txt" EXISTING_DEPS_TXT)
  else()
    set(EXISTING_DEPS_TXT "")
  endif()
  
  if(NOT EXISTING_DEPS_TXT STREQUAL DEPS_TXT_CONTENT)
    file(WRITE "${onnxruntime_SOURCE_DIR}/cmake/deps.txt" "${DEPS_TXT_CONTENT}")
    message(STATUS "Updated onnxruntime deps.txt to use local archives")
  endif()
  
  if(TARGET nlohmann_json::nlohmann_json)
    file(READ "${onnxruntime_SOURCE_DIR}/cmake/external/onnxruntime_external_deps.cmake" EXTERNAL_DEPS_CONTENT)
    if(NOT EXTERNAL_DEPS_CONTENT MATCHES "nlohmann_json already provided by main project")
      string(REPLACE 
        "onnxruntime_fetchcontent_makeavailable(nlohmann_json)" 
        "# nlohmann_json already provided by main project
if(NOT TARGET nlohmann_json::nlohmann_json)
  onnxruntime_fetchcontent_makeavailable(nlohmann_json)
endif()"
        EXTERNAL_DEPS_CONTENT "${EXTERNAL_DEPS_CONTENT}")
      file(WRITE "${onnxruntime_SOURCE_DIR}/cmake/external/onnxruntime_external_deps.cmake" "${EXTERNAL_DEPS_CONTENT}")
      message(STATUS "Patched onnxruntime to skip nlohmann_json (reusing existing target)")
    endif()
  endif()
  
  file(READ "${onnxruntime_SOURCE_DIR}/cmake/CMakeLists.txt" ORT_CMAKE_CONTENT)
  if(ORT_CMAKE_CONTENT MATCHES "cmake_policy\\(SET CMP0104 OLD\\)")
    string(REPLACE 
      "cmake_policy(SET CMP0104 OLD)" 
      "# cmake_policy(SET CMP0104 OLD) - disabled"
      ORT_CMAKE_CONTENT "${ORT_CMAKE_CONTENT}")
    file(WRITE "${onnxruntime_SOURCE_DIR}/cmake/CMakeLists.txt" "${ORT_CMAKE_CONTENT}")
    message(STATUS "Patched onnxruntime to disable CMP0104 OLD policy")
  endif()
  
  file(READ "${onnxruntime_SOURCE_DIR}/cmake/external/onnxruntime_external_deps.cmake" EXTERNAL_DEPS_CONTENT)
  if(NOT EXTERNAL_DEPS_CONTENT MATCHES "set.*DEP_URL_psimd")
    string(REPLACE 
      "if(onnxruntime_USE_COREML)"
      "set(DEP_URL_psimd \"file://${DEPS_DIR}/psimd-2024-07-02.zip\")
if(onnxruntime_USE_COREML)"
      EXTERNAL_DEPS_CONTENT "${EXTERNAL_DEPS_CONTENT}")
    file(WRITE "${onnxruntime_SOURCE_DIR}/cmake/external/onnxruntime_external_deps.cmake" "${EXTERNAL_DEPS_CONTENT}")
    message(STATUS "Patched onnxruntime to add DEP_URL_psimd")
  endif()
  
  add_subdirectory(${onnxruntime_SOURCE_DIR}/cmake ${onnxruntime_BINARY_DIR} EXCLUDE_FROM_ALL)
  
  set(CMAKE_CXX_STANDARD ${CMAKE_CXX_STANDARD_BACKUP})

  yspeech_set_folder_for_targets_in_dir(${onnxruntime_SOURCE_DIR}/cmake "3rdparty")
endif()

if(TARGET onnxruntime)
  get_target_property(ONNXRUNTIME_LIBRARY onnxruntime IMPORTED_LOCATION)
  if(NOT ONNXRUNTIME_LIBRARY)
    set(ONNXRUNTIME_LIBRARY onnxruntime)
  endif()
  get_target_property(ONNXRUNTIME_INCLUDE_DIR onnxruntime INTERFACE_INCLUDE_DIRECTORIES)
  if(NOT ONNXRUNTIME_INCLUDE_DIR)
    set(ONNXRUNTIME_INCLUDE_DIR ${onnxruntime_SOURCE_DIR}/include)
  endif()
else()
  set(ONNXRUNTIME_LIBRARY ${onnxruntime_BINARY_DIR}/libonnxruntime.a)
  set(ONNXRUNTIME_INCLUDE_DIR ${onnxruntime_SOURCE_DIR}/include)
endif()

set(ONNXRUNTIME_LIBRARY onnxruntime CACHE INTERNAL "ONNX Runtime library")
set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_INCLUDE_DIR}" CACHE INTERNAL "ONNX Runtime include directory")
set(onnxruntime_SOURCE_DIR "${onnxruntime_SOURCE_DIR}" CACHE INTERNAL "ONNX Runtime source directory")

message(STATUS "  Library: ${ONNXRUNTIME_LIBRARY}")
message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIR}")

if(YSPEECH_BUILD_DEPS)
  add_custom_target(install_onnxruntime
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/lib"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/include/onnxruntime"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${onnxruntime_SOURCE_DIR}/include/onnxruntime" "${LIB_DIR}/include/onnxruntime"
    COMMAND ${CMAKE_COMMAND} 
      -DCACHE_DIR="${FETCHCONTENT_BASE_DIR}"
      -DLIB_DIR="${LIB_DIR}/lib"
      -P "${CMAKE_SOURCE_DIR}/cmake/utils/install_libs.cmake"
    COMMENT "Installing ONNX Runtime to ${LIB_DIR}"
  )
  set_target_properties(install_onnxruntime PROPERTIES FOLDER "CMake/Install")
  
  add_dependencies(yspeech_install_3rd_lib install_onnxruntime)
endif()
