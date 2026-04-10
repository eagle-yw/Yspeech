
add_library(yspeech)

target_sources(yspeech 
    PUBLIC FILE_SET CXX_MODULES BASE_DIRS . FILES
    yspeech/types.cppm
    yspeech/ring_buffer.cppm
    yspeech/error.cppm
    yspeech/data_keys.cppm
    yspeech/log.cppm
    yspeech/capability.cppm
    yspeech/capabilities/alert.cppm
    yspeech/capabilities/logger.cppm
    yspeech/capabilities/status.cppm
    yspeech/aspect.cppm
    yspeech/aspects/logger.cppm
    yspeech/aspects/timer.cppm
    yspeech/stream_process.cppm
    yspeech/onnx/ort_symbol_lookup.cppm
    yspeech/domain/source/base.cppm
    yspeech/domain/source/builtin.cppm
    yspeech/domain/source/stage.cppm
    yspeech/domain/vad/base.cppm
    yspeech/domain/vad/silero.cppm
    yspeech/domain/vad/stage.cppm
    yspeech/domain/asr/base.cppm
    yspeech/domain/asr/paraformer.cppm
    yspeech/domain/asr/stage.cppm
    yspeech/domain/asr/whisper.cppm
    yspeech/domain/asr/sensevoice.cppm
    yspeech/domain/feature/base.cppm
    yspeech/domain/feature/kaldi_fbank.cppm
    yspeech/domain/feature/stage.cppm
    yspeech/runtime/token.cppm
    yspeech/runtime/segment_state.cppm
    yspeech/runtime/segment_registry.cppm
    yspeech/runtime/runtime_context.cppm
    yspeech/runtime/stage_adapter.cppm
    yspeech/runtime/stage_support.cppm
    yspeech/runtime/event_record.cppm
    yspeech/runtime/pipeline_recipe.cppm
    yspeech/runtime/runtime_dag.cppm
    yspeech/runtime/runtime_dag_executor.cppm
    yspeech/runtime/pipeline_builder.cppm
    yspeech/runtime/pipeline_executor.cppm
    yspeech/runtime/event_stage.cppm
    yspeech/pipeline_config.cppm
    yspeech/frame_source.cppm
    yspeech/engine_runtime.cppm
    yspeech/engine.cppm
    yspeech/audio.cppm
    yspeech/audio/audio_file.cppm
    yspeech/performance_export.cppm
    yspeech/resource_monitor.cppm
    yspeech/runtime/common.cppm
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
