module;

export module yspeech.runtime.token;

import std;
import yspeech.types;

namespace yspeech {

export using PipelineTokenId = std::uint64_t;
export using SegmentId = std::uint64_t;

export enum class PipelineTokenKind {
    AudioWindow,
    SegmentUpdate,
    SegmentFinal,
    EndOfStream
};

export struct PipelineToken {
    PipelineTokenId token_id = 0;
    std::size_t line_id = 0;
    std::string stream_id = "default";
    PipelineTokenKind kind = PipelineTokenKind::AudioWindow;

    std::int64_t pts_begin_ms = 0;
    std::int64_t pts_end_ms = 0;
    bool eos = false;

    std::vector<float> audio;
    std::optional<SegmentId> segment_id;
    std::optional<VadSegment> vad_segment;
    std::optional<AsrResult> asr_result;
    std::optional<AsrResult> stream_asr_result;
    bool stream_final = false;

    std::vector<std::vector<float>> feature_frames;
    std::uint64_t feature_version = 0;
    std::uint64_t partial_version = 0;

    bool valid() const noexcept {
        return eos || !audio.empty() || segment_id.has_value();
    }

    void mark_eos() {
        eos = true;
        kind = PipelineTokenKind::EndOfStream;
    }
};

}
