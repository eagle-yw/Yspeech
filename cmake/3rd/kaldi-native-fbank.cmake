# kaldi-native-fbank CMake configuration
# A standalone library for audio feature extraction, compatible with Kaldi

if(YSPEECH_BUILD_DEPS)
    FetchContent_Declare(
        kaldi-native-fbank
        GIT_REPOSITORY https://github.com/csukuangfj/kaldi-native-fbank.git
        GIT_TAG v1.20.0
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR cmake  # Use cmake subdirectory which contains the main CMakeLists.txt
        EXCLUDE_FROM_ALL
    )

    FetchContent_MakeAvailable(kaldi-native-fbank)

    # Install headers and library to 3rd-lib
    add_custom_command(TARGET yspeech_install_3rd_lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${kaldi-native-fbank_SOURCE_DIR}/kaldi-native-fbank/csrc
            ${LIB_DIR}/include/kaldi-native-fbank
        COMMENT "Installing kaldi-native-fbank headers"
    )

    # Build and install the library
    add_custom_command(TARGET yspeech_install_3rd_lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} --build ${kaldi-native-fbank_BINARY_DIR} --target kaldi-native-fbank-core
        COMMAND ${CMAKE_COMMAND} -E copy
            ${kaldi-native-fbank_BINARY_DIR}/libkaldi-native-fbank-core.a
            ${LIB_DIR}/lib/
        COMMENT "Installing kaldi-native-fbank library"
    )
else()
    # Use prebuilt library
    add_library(kaldi-native-fbank-core STATIC IMPORTED)
    set_target_properties(kaldi-native-fbank-core PROPERTIES
        IMPORTED_LOCATION ${LIB_DIR}/lib/libkaldi-native-fbank-core.a
        INTERFACE_INCLUDE_DIRECTORIES ${LIB_DIR}/include
    )
endif()

# Set variables for use in other CMake files
set(KALDI_NATIVE_FBANK_INCLUDE_DIR ${LIB_DIR}/include)
set(KALDI_NATIVE_FBANK_LIBRARY ${LIB_DIR}/lib/libkaldi-native-fbank-core.a)

message(STATUS "kaldi-native-fbank include: ${KALDI_NATIVE_FBANK_INCLUDE_DIR}")
message(STATUS "kaldi-native-fbank library: ${KALDI_NATIVE_FBANK_LIBRARY}")
