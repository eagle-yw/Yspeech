module;

#include <nlohmann/json.hpp>

export module yspeech.runtime.event_stage;

import std;
import yspeech.runtime.runtime_context;
import yspeech.runtime.segment_registry;
import yspeech.runtime.token;
import yspeech.types;

namespace yspeech {

export class EventStage {
public:
    void init(const nlohmann::json& config) {
        config_ = config;
    }

    void process(PipelineToken& token, RuntimeContext& runtime, SegmentRegistry& registry) {
        if (!runtime.emit_event) {
            return;
        }

        if (token.vad_segment.has_value()) {
            runtime.emit_event(EngineEvent{
                .kind = EngineEventKind::VadEnd,
                .task = runtime.task,
                .source_id = token.stream_id,
                .pts_ms = token.vad_segment->end_ms,
                .vad_segment = token.vad_segment
            });
        }

        if (token.asr_result.has_value()) {
            const auto kind = (token.eos || token.kind == PipelineTokenKind::SegmentFinal)
                ? EngineEventKind::ResultSegmentFinal
                : EngineEventKind::ResultPartial;
            runtime.emit_event(EngineEvent{
                .kind = kind,
                .task = runtime.task,
                .source_id = token.stream_id,
                .pts_ms = token.pts_end_ms,
                .asr = token.asr_result,
                .vad_segment = token.vad_segment
            });
        }

        if (token.stream_final && token.stream_asr_result.has_value()) {
            runtime.emit_event(EngineEvent{
                .kind = EngineEventKind::ResultStreamFinal,
                .task = runtime.task,
                .source_id = token.stream_id,
                .pts_ms = token.pts_end_ms,
                .asr = token.stream_asr_result,
                .vad_segment = token.vad_segment
            });
        }

        if (token.kind == PipelineTokenKind::SegmentFinal || token.stream_final) {
            registry.erase_closed();
        }
    }

private:
    nlohmann::json config_;
};

}
