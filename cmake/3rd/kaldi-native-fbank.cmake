set(KALDI_NATIVE_FBANK_ARCHIVE "${ARCHIVE_DIR}/kaldi-native-fbank-1.20.0.tar.gz" CACHE FILEPATH "kaldi-native-fbank 1.20.0 archive path")
if(NOT EXISTS "${KALDI_NATIVE_FBANK_ARCHIVE}")
  message(STATUS "kaldi-native-fbank archive not found, will download from GitHub...")
  set(KALDI_NATIVE_FBANK_URL "https://github.com/csukuangfj/kaldi-native-fbank/archive/refs/tags/v1.20.0.tar.gz")
  file(DOWNLOAD "${KALDI_NATIVE_FBANK_URL}" "${KALDI_NATIVE_FBANK_ARCHIVE}"
    SHOW_PROGRESS
    STATUS DOWNLOAD_STATUS
    TIMEOUT 300
  )
  list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
  if(NOT STATUS_CODE EQUAL 0)
    message(FATAL_ERROR "Failed to download kaldi-native-fbank from ${KALDI_NATIVE_FBANK_URL}")
  endif()
endif()

set(PREBUILT_KALDI_NATIVE_FBANK_LIB "${LIB_DIR}/lib/libkaldi-native-fbank-core.a")

if(EXISTS "${PREBUILT_KALDI_NATIVE_FBANK_LIB}")
  add_library(kaldi-native-fbank-core STATIC IMPORTED)
  set_target_properties(kaldi-native-fbank-core PROPERTIES
    IMPORTED_LOCATION "${PREBUILT_KALDI_NATIVE_FBANK_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIB_DIR}/include"
  )
  
  set(KALDI_NATIVE_FBANK_INCLUDE_DIR "${LIB_DIR}/include" CACHE INTERNAL "kaldi-native-fbank include directory")
  set(KALDI_NATIVE_FBANK_LIBRARY kaldi-native-fbank-core CACHE INTERNAL "kaldi-native-fbank library")
  
  message(STATUS "Using prebuilt kaldi-native-fbank from ${LIB_DIR}")
  return()
endif()

message(STATUS "Building kaldi-native-fbank from source...")

set(KALDI_NATIVE_FBANK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(KALDI_NATIVE_FBANK_BUILD_PYTHON OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  kaldi_native_fbank
  URL "${KALDI_NATIVE_FBANK_ARCHIVE}"
  URL_HASH SHA256=c6195b3cf374eef824644061d3c04f6b2a9267ae554169cbaa9865c89c1fe4f9
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(kaldi_native_fbank)
# NOTE:
# kaldi-native-fbank's SOURCE_SUBDIR layout can make DIRECTORY-scope target
# introspection fragile across generators/CMake versions. Skip folder tagging
# here to keep configuration robust.

set(KALDI_NATIVE_FBANK_INCLUDE_DIR "${kaldi_native_fbank_SOURCE_DIR}" CACHE INTERNAL "kaldi-native-fbank include directory")
set(KALDI_NATIVE_FBANK_LIBRARY kaldi-native-fbank-core CACHE INTERNAL "kaldi-native-fbank library")

if(YSPEECH_BUILD_DEPS)
  add_custom_target(install_kaldi_native_fbank
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/lib"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/include/kaldi-native-fbank/csrc"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:kaldi-native-fbank-core>" "${LIB_DIR}/lib/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${kaldi_native_fbank_SOURCE_DIR}/kaldi-native-fbank/csrc" "${LIB_DIR}/include/kaldi-native-fbank/csrc"
    COMMENT "Installing kaldi-native-fbank to ${LIB_DIR}"
  )
  set_target_properties(install_kaldi_native_fbank PROPERTIES FOLDER "CMake/Install")
  add_dependencies(yspeech_install_3rd_lib install_kaldi_native_fbank)
  add_dependencies(install_kaldi_native_fbank kaldi-native-fbank-core)
endif()

message(STATUS "  Library: ${KALDI_NATIVE_FBANK_LIBRARY}")
message(STATUS "  Include: ${KALDI_NATIVE_FBANK_INCLUDE_DIR}")
