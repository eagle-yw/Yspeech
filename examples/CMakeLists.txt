cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

project(YspeechExamples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(TARGET PROPERTY CXX_MODULE_STD ON)

find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)

add_executable(simple_transcribe simple_transcribe.cpp)
target_link_libraries(simple_transcribe PRIVATE yspeech ${FOUNDATION_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
set_target_properties(simple_transcribe PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
)

add_executable(streaming_demo streaming_demo.cpp)
target_link_libraries(streaming_demo PRIVATE yspeech ${FOUNDATION_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
set_target_properties(streaming_demo PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
)

add_executable(transcribe_tool transcribe_tool.cpp)
target_link_libraries(transcribe_tool PRIVATE yspeech ${FOUNDATION_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
set_target_properties(transcribe_tool PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
)

set_target_properties(simple_transcribe streaming_demo transcribe_tool
    PROPERTIES
    FOLDER "Examples"
)
