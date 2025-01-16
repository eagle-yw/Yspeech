
include(../cmake/import/SetupONNXRuntime.cmake)
setup_onnxruntime(VERSION 1.20.1 PLATFORM linux-x64-gpu UPDATE OFF)

add_library(yspeech)

target_sources(yspeech 
    PUBLIC FILE_SET CXX_MODULES FILES
    yspeech.cppm
    engine.cppm
    context.cppm
    pipeline.cppm
    common/types.ixx
    operator/operator.cppm
    operator/op_vad.cppm
)

target_link_libraries(yspeech ${ONNXRUNTIME_LIBRARY})