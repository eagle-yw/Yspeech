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
        std::vector<std::vector<float>> stream_feature_snapshot;
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
            if (output.has_value() && !output->features.empty()) {
                state.accumulated_features.insert(
                    state.accumulated_features.end(),
                    output->features.begin(),
                    output->features.end()
                );
                state.feature_version = output->version;
            }
            stream_feature_version = state.feature_version;
            stream_feature_snapshot = state.accumulated_features;
        }
        {
            std::scoped_lock lock(runtime.stream_feature_mutex);
            runtime.stream_feature_snapshots[token.stream_id] = RuntimeContext::StreamFeatureSnapshot{
                .features = stream_feature_snapshot,
                .version = stream_feature_version,
                .feature_count = static_cast<int>(stream_feature_snapshot.size())
            };
        }
        if (!token.segment_id.has_value()) {
            return;
        }

        auto segment = registry.get(*token.segment_id);
        if (!segment) {
            return;
        }

        if (output.has_value()) {
            token.feature_version = stream_feature_version;
        }
        if (!output.has_value() || output->features.empty()) {
            return;
        }

        {
            std::lock_guard lock(segment->mutex);
            segment->features_accumulated = stream_feature_snapshot;
            segment->audio_samples_consumed_by_feature = segment->audio_accumulated.size();
            segment->feature_version = stream_feature_version;
            segment->feature_ready = true;
        }

        if (output.has_value()) {
            token.feature_frames.insert(token.feature_frames.end(), output->features.begin(), output->features.end());
        }
        token.feature_version = stream_feature_version;
        log_debug("FeatureStage produced {} frames for stream {} segment {}", output->num_frames, token.stream_id, *token.segment_id);
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
    struct StreamFeatureState {
        std::unique_ptr<FeatureCoreIface> core;
        bool initialized = false;
        std::vector<std::vector<float>> accumulated_features;
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
