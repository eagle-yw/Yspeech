enable_testing()

add_executable(test 
    test.cpp
    test_op.cpp
    test_audio.cpp
    test_aspect.cpp
    test_capability.cpp
    test_error.cpp
    test_audio_buffer.cpp
    test_real_audio.cpp
)

target_include_directories(test PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(test 
    gtest 
    gtest_main
    gmock
    gmock_main
    yspeech
)

set_property(TARGET test PROPERTY CXX_MODULE_STD ON)
