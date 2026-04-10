module;

#include <nlohmann/json.hpp>

export module yspeech.domain.feature.stage;

import std;
import yspeech.log;
import yspeech.domain.feature.base;
import yspeech.domain.feature.kaldi_fbank;
import yspeech.runtime.runtime_context;
import yspeech.runtime.segment_state;
import yspeech.runtime.segment_registry;
import yspeech.runtime.stage_adapter;
import yspeech.runtime.stage_support;
import yspeech.runtime.token;
import yspeech.types;

namespace yspeech {

export class FeatureStage {
public:
    void init(const nlohmann::json& config) {
        core_name_ = config.value("core_name", std::string("KaldiFbank"));
        adapter_.init(config, config.value("__core_id", core_id_));
    }

    void process(PipelineToken& token, RuntimeContext& runtime, SegmentRegistry& registry) {
        if (token.audio.empty() && !token.eos) {
            return;
        }

        std::optional<KaldiFbankOutput> output;
        std::uint64_t stream_feature_version = 0;
        LocalFeatureChunkListPtr stream_feature_chunks;
        LocalFeatureChunkListPtr delta_feature_chunks;
        int stream_feature_count = 0;
        int delta_feature_count = 0;
        std::shared_ptr<const std::vector<std::vector<float>>> produced_chunk;
        bool snapshot_changed = false;
        {
            std::scoped_lock lock(core_mutex_);
            auto& state = stream_states_[token.stream_id];
            auto* core = state.ensure(
                [&]() {
                    return FeatureCoreFactory::get_instance().create_core(core_name_);
                },
                [&](FeatureCoreIface& created_core) {
                    created_core.init(adapter_.config());
                    stats_binding_.bind_core(created_core);
                }
            );
            if (!core) {
                return;
            }
            output = adapter_.run(runtime, [&]() {
                return core->process_samples(std::span<const float>(token.audio), token.eos);
            });
            if (output.has_value() && !output->delta_features.empty()) {
                produced_chunk = std::make_shared<const std::vector<std::vector<float>>>(
                    std::move(output->delta_features)
                );
                if (!state.feature_chunks) {
                    state.feature_chunks = std::make_shared<LocalFeatureChunkList>();
                } else if (state.feature_chunks.use_count() > 1) {
                    state.feature_chunks = std::make_shared<LocalFeatureChunkList>(*state.feature_chunks);
                }
                state.feature_chunks->push_back(produced_chunk);
                state.feature_count += static_cast<int>(produced_chunk->size());
                state.feature_version = output->version;
                delta_feature_count = output->delta_num_frames;
                auto delta_chunks = std::make_shared<LocalFeatureChunkList>();
                delta_chunks->push_back(produced_chunk);
                delta_feature_chunks = std::move(delta_chunks);
                snapshot_changed = true;
            }
            stream_feature_version = state.feature_version;
            stream_feature_count = state.feature_count;
            if (snapshot_changed) {
                stream_feature_chunks = state.feature_chunks;
            }
        }
        if (snapshot_changed) {
            std::scoped_lock lock(runtime.stream_feature_mutex);
            runtime.stream_feature_snapshots[token.stream_id] = RuntimeContext::StreamFeatureSnapshot{
                .chunks = stream_feature_chunks,
                .delta_chunks = delta_feature_chunks,
                .version = stream_feature_version,
                .feature_count = stream_feature_count,
                .delta_feature_count = delta_feature_count
            };
        }
        if (output.has_value()) {
            token.feature_version = stream_feature_version;
        }
        if (!token.segment_id.has_value()) {
            return;
        }

        auto segment = registry.get(*token.segment_id);
        if (!segment) {
            return;
        }

        {
            std::lock_guard lock(segment->mutex);
            segment->audio_samples_consumed_by_feature = segment->audio_accumulated.size();
            segment->feature_version = stream_feature_version;
            segment->feature_ready = true;
            if (segment->lifecycle == SegmentLifecycle::Closed) {
                segment->audio_accumulated.clear();
                segment->audio_accumulated.shrink_to_fit();
                segment->features_accumulated.clear();
                segment->features_accumulated.shrink_to_fit();
            }
        }

        if (produced_chunk && !produced_chunk->empty()) {
            token.feature_frames.insert(token.feature_frames.end(), produced_chunk->begin(), produced_chunk->end());
        }
        token.feature_version = stream_feature_version;
        log_debug(
            "FeatureStage advanced stream {} to feature version {} for segment {}",
            token.stream_id,
            stream_feature_version,
            *token.segment_id
        );
    }

    void deinit() {
        for (auto& [stream_id, state] : stream_states_) {
            (void)stream_id;
            deinit_core(state.core);
            state.reset();
        }
        stream_states_.clear();
        adapter_.reset();
    }

    void bind_stats(ProcessingStats* stats) {
        stats_binding_.bind(stats);
        for (auto& [stream_id, state] : stream_states_) {
            (void)stream_id;
            stats_binding_.bind_core_ptr(state.core);
        }
    }

private:
    using LocalFeatureChunk = std::shared_ptr<const std::vector<std::vector<float>>>;
    using LocalFeatureChunkList = std::vector<LocalFeatureChunk>;
    using LocalFeatureChunkListPtr = std::shared_ptr<LocalFeatureChunkList>;

    struct StreamFeatureState : LazyCoreHolder<FeatureCoreIface> {
        LocalFeatureChunkListPtr feature_chunks = std::make_shared<LocalFeatureChunkList>();
        int feature_count = 0;
        std::uint64_t feature_version = 0;
    };

    std::string core_name_ = "KaldiFbank";
    std::unordered_map<std::string, StreamFeatureState> stream_states_;
    StageStatsBinding stats_binding_;
    std::string core_id_ = "fbank";
    std::mutex core_mutex_;
    StageAdapter adapter_;
};

}
