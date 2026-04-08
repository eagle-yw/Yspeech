
add_library(yspeech)

target_sources(yspeech 
    PUBLIC FILE_SET CXX_MODULES BASE_DIRS . FILES
    yspeech/types.cppm
    yspeech/ring_buffer.cppm
    yspeech/frame_ring.cppm
    yspeech/stream_store.cppm
    yspeech/error.cppm
    yspeech/state.cppm
    yspeech/data_keys.cppm
    yspeech/context.cppm
    yspeech/log.cppm
    yspeech/capability.cppm
    yspeech/aspect.cppm
    yspeech/aspects/logger.cppm
    yspeech/aspects/timer.cppm
    yspeech/op.cppm
    yspeech/op/ort_symbol_lookup.cppm
    yspeech/op/vad/vad.cppm
    yspeech/op/vad/silero.cppm
    yspeech/op/asr/base.cppm
    yspeech/op/asr/paraformer.cppm
    yspeech/op/asr/whisper.cppm
    yspeech/op/asr/sensevoice.cppm
    yspeech/op/feature/kaldi_fbank.cppm
    yspeech/pipeline_config.cppm
    yspeech/pipeline_manager.cppm
    yspeech/frame_source.cppm
    yspeech/engine_runtime.cppm
    yspeech/engine.cppm
    yspeech/audio.cppm
    yspeech/audio/audio_file.cppm
    yspeech/performance_export.cppm
    yspeech/resource_monitor.cppm
    yspeech/performance_alert.cppm
    yspeech/runtime_common.cppm
    yspeech/operators.cppm
    yspeech/yspeech.cppm
)

target_link_libraries(yspeech 
    PRIVATE
        onnxruntime
        taskflow
        spdlog::spdlog
        kaldi-native-fbank-core
    PUBLIC
        nlohmann_json::nlohmann_json
)

target_include_directories(yspeech PRIVATE 
    ${KALDI_NATIVE_FBANK_INCLUDE_DIR}
    ${onnxruntime_SOURCE_DIR}/include/onnxruntime/core/session
)

set_property(TARGET yspeech PROPERTY CXX_MODULE_STD ON)
set_target_properties(yspeech PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
