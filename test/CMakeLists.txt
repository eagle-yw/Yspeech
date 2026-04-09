enable_testing()

add_executable(test
    test.cpp
    test_audio.cpp
    test_audio_buffer.cpp
    test_real_audio.cpp
    test_silero_vad.cpp
    test_silero_vad_simple.cpp
    test_asr.cpp
    test_integration_vad_asr.cpp
    test_asr_real_audio.cpp
    test_kaldi_fbank_op.cpp
    test_kaldi_fbank_op_extended.cpp
    test_kaldi_fbank_performance.cpp
    test_pipeline_runtime.cpp
)

target_include_directories(test PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(test
    gmock_main
    yspeech
)

if(APPLE)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(FOUNDATION_LIBRARY Foundation)
    target_link_libraries(test
        ${COREFOUNDATION_LIBRARY}
        ${FOUNDATION_LIBRARY}
    )
endif()

set_property(TARGET test PROPERTY CXX_MODULE_STD ON)
