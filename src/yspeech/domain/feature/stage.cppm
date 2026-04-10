module;

#include <nlohmann/json.hpp>

export module yspeech.domain.feature.stage;

import std;
import yspeech.aspect;
import yspeech.aspect.logger;
import yspeech.aspect.timer;
import yspeech.capability;
import yspeech.log;
import yspeech.domain.feature.base;
import yspeech.domain.feature.kaldi_fbank;
import yspeech.runtime.runtime_context;
import yspeech.runtime.segment_state;
import yspeech.runtime.segment_registry;
import yspeech.runtime.token;
import yspeech.types;

namespace yspeech {

export class FeatureStage {
public:
    void init(const nlohmann::json& config) {
        config_ = config;
        core_name_ = config.value("core_name", std::string("KaldiFbank"));
        core_id_ = config.value("__core_id", core_id_);
        configure_aspects(config);
        configure_capabilities(config);
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
            if (!state.initialized) {
                state.core = FeatureCoreFactory::get_instance().create_core(core_name_);
                state.core->init(config_);
                state.initialized = true;
            }
            if (!state.core) {
                return;
            }
            apply_capabilities(pre_capabilities_, runtime);
            auto aspect_payloads = before_aspects(runtime);
            output = state.core->process_samples(std::span<const float>(token.audio), token.eos);
            after_aspects(runtime, std::move(aspect_payloads));
            apply_capabilities(post_capabilities_, runtime);
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
            if (state.core) {
                state.core->deinit();
            }
        }
        stream_states_.clear();
    }

    void bind_stats(ProcessingStats* stats) {
        config_runtime_stats_ = stats;
    }

private:
    using LocalFeatureChunk = std::shared_ptr<const std::vector<std::vector<float>>>;
    using LocalFeatureChunkList = std::vector<LocalFeatureChunk>;
    using LocalFeatureChunkListPtr = std::shared_ptr<LocalFeatureChunkList>;

    struct StreamFeatureState {
        std::unique_ptr<FeatureCoreIface> core;
        bool initialized = false;
        LocalFeatureChunkListPtr feature_chunks = std::make_shared<LocalFeatureChunkList>();
        int feature_count = 0;
        std::uint64_t feature_version = 0;
    };

    nlohmann::json config_;
    std::string core_name_ = "KaldiFbank";
    std::unordered_map<std::string, StreamFeatureState> stream_states_;
    std::vector<AspectIface> aspects_;
    std::vector<CapabilityIface> pre_capabilities_;
    std::vector<CapabilityIface> post_capabilities_;
    ProcessingStats* config_runtime_stats_ = nullptr;
    std::string core_id_ = "fbank";
    std::mutex core_mutex_;

    void configure_aspects(const nlohmann::json& config) {
        aspects_.clear();
        aspects_.emplace_back(TimerAspect{});
        if (config.contains("aspects") && config["aspects"].is_array()) {
            for (const auto& entry : config["aspects"]) {
                if (!entry.is_string()) {
                    continue;
                }
                if (entry.get<std::string>() == "LoggerAspect") {
                    aspects_.emplace_back(LoggerAspect{});
                }
            }
        }
    }

    void configure_capabilities(const nlohmann::json& config) {
        pre_capabilities_.clear();
        post_capabilities_.clear();
        if (!config.contains("capabilities") || !config["capabilities"].is_array()) {
            return;
        }
        for (const auto& entry : config["capabilities"]) {
            if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string()) {
                continue;
            }
            auto params = entry.contains("params") && entry["params"].is_object()
                ? entry["params"]
                : nlohmann::json::object();
            params["__component_name"] = core_id_;
            auto capability = CapabilityFactory::get_instance().create_capability(
                entry["name"].get<std::string>(),
                params
            );
            if (capability.phase() == CapabilityPhase::Post) {
                post_capabilities_.push_back(std::move(capability));
            } else {
                pre_capabilities_.push_back(std::move(capability));
            }
        }
    }

    auto before_aspects(RuntimeContext& runtime) -> std::vector<std::any> {
        std::vector<std::any> payloads;
        payloads.reserve(aspects_.size());
        for (auto& aspect : aspects_) {
            payloads.push_back(aspect.before(runtime, core_id_));
        }
        return payloads;
    }

    void after_aspects(RuntimeContext& runtime, std::vector<std::any> payloads) {
        for (std::size_t i = aspects_.size(); i > 0; --i) {
            aspects_[i - 1].after(runtime, core_id_, std::move(payloads[i - 1]));
        }
    }

    void apply_capabilities(std::vector<CapabilityIface>& capabilities, RuntimeContext& runtime) {
        for (auto& capability : capabilities) {
            capability.apply(runtime);
        }
    }
};

}
