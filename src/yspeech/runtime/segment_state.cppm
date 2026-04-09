module;

export module yspeech.runtime.segment_state;

import std;
import yspeech.types;
import yspeech.runtime.token;

namespace yspeech {

export enum class SegmentLifecycle {
    Open,
    Finalizing,
    Closed
};

export struct SegmentState {
    mutable std::mutex mutex;

    SegmentId segment_id = 0;
    std::string stream_id = "default";
    SegmentLifecycle lifecycle = SegmentLifecycle::Open;

    std::int64_t start_ms = 0;
    std::int64_t end_ms = 0;
    bool final_emitted = false;
    int sample_rate = 16000;
    int channels = 1;

    std::vector<float> audio_accumulated;
    std::vector<std::vector<float>> features_accumulated;

    std::size_t audio_samples_consumed_by_feature = 0;
    std::uint64_t feature_version = 0;
    std::uint64_t partial_version = 0;
    bool feature_ready = false;
    std::uint64_t last_asr_feature_version = 0;
    std::size_t last_asr_feature_count = 0;
    std::uint64_t finalized_asr_feature_version = 0;

    std::optional<VadSegment> vad_segment;
    std::optional<AsrResult> last_partial;
    std::optional<AsrResult> final_result;
};

}
