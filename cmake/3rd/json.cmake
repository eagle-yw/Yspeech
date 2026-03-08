set(NLOHMANN_JSON_ARCHIVE "${ARCHIVE_DIR}/json-3.12.0.tar.gz" CACHE FILEPATH "nlohmann_json 3.12.0 archive path")
if(NOT EXISTS "${NLOHMANN_JSON_ARCHIVE}")
  message(FATAL_ERROR "nlohmann_json archive not found: ${NLOHMANN_JSON_ARCHIVE}")
endif()

set(PREBUILT_NLOHMANN_JSON_DIR "${LIB_DIR}/include")
if(EXISTS "${PREBUILT_NLOHMANN_JSON_DIR}/nlohmann/json.hpp")
  add_library(nlohmann_json INTERFACE)
  target_include_directories(nlohmann_json INTERFACE "${PREBUILT_NLOHMANN_JSON_DIR}")
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
  message(STATUS "Using prebuilt nlohmann_json from ${PREBUILT_NLOHMANN_JSON_DIR}")
  return()
endif()

message(STATUS "Building nlohmann_json from source...")

FetchContent_Declare(
  nlohmann_json
  URL "${NLOHMANN_JSON_ARCHIVE}"
  URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(nlohmann_json)
yspeech_set_folder_for_targets_in_dir(${nlohmann_json_SOURCE_DIR} "3rdparty")

if(YSPEECH_BUILD_DEPS)
  add_custom_target(install_nlohmann_json
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIB_DIR}/include"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${nlohmann_json_SOURCE_DIR}/single_include/nlohmann" "${LIB_DIR}/include/nlohmann"
    COMMENT "Installing nlohmann_json to ${LIB_DIR}"
  )
  set_target_properties(install_nlohmann_json PROPERTIES FOLDER "CMake/Install")
  add_dependencies(yspeech_install_3rd_lib install_nlohmann_json)
endif()
