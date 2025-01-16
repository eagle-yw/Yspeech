enable_testing()

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)


add_executable(test 
    test.cpp
    test_op.cpp
)

target_link_libraries(test 
    gtest 
    gtest_main
    yspeech
)