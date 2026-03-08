
add_library(yspeech)

target_sources(yspeech 
    PUBLIC FILE_SET CXX_MODULES BASE_DIRS . FILES
    yspeech/types.cppm
    yspeech/ring_buffer.cppm
    yspeech/error.cppm
    yspeech/state.cppm
    yspeech/context.cppm
    yspeech/log.cppm
    yspeech/capability.cppm
    yspeech/aspect.cppm
    yspeech/aspects/logger.cppm
    yspeech/aspects/timer.cppm
    yspeech/op.cppm
    yspeech/op/vad/vad.cppm
    yspeech/op/vad/silero_vad.cppm
    yspeech/op/audio/audio_input.cppm
    yspeech/op/asr/asr_base.cppm
    yspeech/op/asr/asr_paraformer.cppm
    yspeech/op/asr/asr_whisper.cppm
    yspeech/op/asr/asr_sensevoice.cppm
    yspeech/op/feature/feature_extract.cppm
    yspeech/op/feature/kaldi_fbank.cppm
    yspeech/pipeline_config.cppm
    yspeech/pipeline.cppm
    yspeech/pipeline_manager.cppm
    yspeech/stream_controller.cppm
    yspeech/engine.cppm
    yspeech/audio.cppm
    yspeech/audio/audio_file.cppm
    yspeech/offline_asr.cppm
    yspeech/streaming_asr.cppm
    yspeech/speech_processor.cppm
    yspeech/yspeech.cppm
)

target_link_libraries(yspeech 
    PRIVATE
        onnxruntime
        taskflow
        glog::glog
        kaldi-native-fbank-core
    PUBLIC
        nlohmann_json::nlohmann_json
)

target_include_directories(yspeech PRIVATE 
    ${onnxruntime_SOURCE_DIR}/include/onnxruntime/core/session
)

set_property(TARGET yspeech PROPERTY CXX_MODULE_STD ON)
set_target_properties(yspeech PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
