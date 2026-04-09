module;

#include <nlohmann/json.hpp>

export module yspeech.domain.vad.stage;

import std;
import yspeech.aspect;
import yspeech.aspect.logger;
import yspeech.aspect.timer;
import yspeech.capability;
import yspeech.log;
import yspeech.domain.vad.base;
import yspeech.domain.vad.silero;
import yspeech.runtime.runtime_context;
import yspeech.runtime.segment_state;
import yspeech.runtime.segment_registry;
import yspeech.runtime.token;
import yspeech.types;

namespace yspeech {

export class VadStage {
public:
    void init(const nlohmann::json& config) {
        config_ = config;
        core_name_ = config.value("core_name", std::string("SileroVad"));
        sample_rate_ = config.value("sample_rate", sample_rate_);
        channels_ = config.value("channels", channels_);
        min_silence_duration_ms_ = config.value("min_silence_duration_ms", min_silence_duration_ms_);
        op_id_ = config.value("__op_id", op_id_);
        core_ = VadCoreFactory::get_instance().create_core(core_name_);
        core_->init(config);
        configure_aspects(config);
        configure_capabilities(config);
    }

    void process(PipelineToken& token, RuntimeContext& runtime, SegmentRegistry& registry) {
        if (token.audio.empty() && !token.eos) {
            return;
        }

        if (!token.audio.empty()) {
            recent_audio_.push_back(AudioSlice{
                .pts_begin_ms = token.pts_begin_ms,
                .pts_end_ms = token.pts_end_ms,
                .samples = token.audio
            });
        }

        if (!core_) {
            return;
        }
        apply_capabilities(pre_capabilities_, runtime);
        auto aspect_payloads = before_aspects(runtime);
        auto result = core_->process_samples(std::span<const float>(token.audio), token.eos);
        after_aspects(runtime, std::move(aspect_payloads));
        apply_capabilities(post_capabilities_, runtime);

        if (token.eos) {
            runtime.eos_seen.store(true, std::memory_order_release);
        }

        token.vad_segment.reset();
        token.segment_id.reset();

        if (result.is_speech) {
            if (!active_segment_id_.has_value()) {
                auto state = registry.create_segment(token.stream_id, result.current_start_ms);
                {
                    std::lock_guard lock(state->mutex);
                    state->sample_rate = sample_rate_;
                    state->channels = channels_;
                    state->start_ms = result.current_start_ms;
                    state->end_ms = token.pts_end_ms;
                    state->lifecycle = SegmentLifecycle::Open;
                }
                active_segment_id_ = state->segment_id;
            }
            if (auto active = registry.get(*active_segment_id_)) {
                std::lock_guard lock(active->mutex);
                active->end_ms = token.pts_end_ms;
                active->audio_accumulated.insert(active->audio_accumulated.end(), token.audio.begin(), token.audio.end());
            }
            token.segment_id = active_segment_id_;
            token.kind = PipelineTokenKind::SegmentUpdate;
        }

        for (const auto& segment : result.finished_segments) {
            std::shared_ptr<SegmentState> state;
            if (active_segment_id_.has_value()) {
                state = registry.get(*active_segment_id_);
            }
            if (!state) {
                state = registry.create_segment(token.stream_id, segment.start_ms);
            }
            {
                std::lock_guard lock(state->mutex);
                state->sample_rate = sample_rate_;
                state->channels = channels_;
                state->start_ms = segment.start_ms;
                state->end_ms = segment.end_ms;
                state->vad_segment = segment;
                state->lifecycle = SegmentLifecycle::Closed;
                if (state->audio_accumulated.empty()) {
                    state->audio_accumulated = collect_segment_audio(segment);
                }
            }
            state->final_emitted = false;
            token.segment_id = state->segment_id;
            token.vad_segment = segment;
            token.kind = PipelineTokenKind::SegmentFinal;
            log_debug("VadStage produced segment {} [{}ms - {}ms]", state->segment_id, segment.start_ms, segment.end_ms);
            active_segment_id_.reset();
        }

        if (token.eos && active_segment_id_.has_value()) {
            if (auto active = registry.get(*active_segment_id_)) {
                std::lock_guard lock(active->mutex);
                active->end_ms = std::max(active->end_ms, token.pts_end_ms);
                active->vad_segment = VadSegment{
                    .start_ms = active->start_ms,
                    .end_ms = active->end_ms,
                    .confidence = core_->current_probability()
                };
                active->lifecycle = SegmentLifecycle::Closed;
                token.segment_id = active->segment_id;
                token.vad_segment = active->vad_segment;
                token.kind = PipelineTokenKind::SegmentFinal;
            }
            registry.close_segment(*active_segment_id_, token.pts_end_ms);
            active_segment_id_.reset();
        } else if (token.eos && token.segment_id.has_value()) {
            registry.close_segment(*token.segment_id, token.pts_end_ms);
        }

        discard_completed_audio();
    }

    void deinit() {
        if (core_) {
            core_->deinit();
        }
        core_.reset();
    }

    void bind_stats(ProcessingStats* stats) {
        runtime_stats_ = stats;
    }

private:
    struct AudioSlice {
        std::int64_t pts_begin_ms = 0;
        std::int64_t pts_end_ms = 0;
        std::vector<float> samples;
    };

    auto collect_segment_audio(const VadSegment& segment) const -> std::vector<float> {
        std::vector<float> audio;
        for (const auto& slice : recent_audio_) {
            const auto overlap_begin = std::max(segment.start_ms, slice.pts_begin_ms);
            const auto overlap_end = std::min(segment.end_ms, slice.pts_end_ms);
            if (overlap_begin >= overlap_end || slice.samples.empty()) {
                continue;
            }

            const auto slice_duration_ms = std::max<std::int64_t>(1, slice.pts_end_ms - slice.pts_begin_ms);
            const auto sample_count = slice.samples.size();
            const auto begin_offset_ms = overlap_begin - slice.pts_begin_ms;
            const auto end_offset_ms = overlap_end - slice.pts_begin_ms;
            const auto begin_index = static_cast<std::size_t>(
                std::clamp<std::int64_t>((begin_offset_ms * static_cast<std::int64_t>(sample_count)) / slice_duration_ms, 0, static_cast<std::int64_t>(sample_count))
            );
            const auto end_index = static_cast<std::size_t>(
                std::clamp<std::int64_t>((end_offset_ms * static_cast<std::int64_t>(sample_count)) / slice_duration_ms, 0, static_cast<std::int64_t>(sample_count))
            );

            if (begin_index < end_index && end_index <= slice.samples.size()) {
                audio.insert(audio.end(), slice.samples.begin() + static_cast<std::ptrdiff_t>(begin_index),
                             slice.samples.begin() + static_cast<std::ptrdiff_t>(end_index));
            }
        }
        return audio;
    }

    void discard_completed_audio() {
        if (recent_audio_.empty()) {
            return;
        }

        auto latest_end_ms = std::int64_t{0};
        for (const auto& slice : recent_audio_) {
            latest_end_ms = std::max(latest_end_ms, slice.pts_end_ms);
        }

        const auto retention_ms = std::max<std::int64_t>(2'000, min_silence_duration_ms_ * 2);
        const auto cutoff_ms = latest_end_ms - retention_ms;
        while (!recent_audio_.empty() && recent_audio_.front().pts_end_ms < cutoff_ms) {
            recent_audio_.pop_front();
        }
    }

    nlohmann::json config_;
    std::string core_name_ = "SileroVad";
    std::unique_ptr<VadCoreIface> core_;
    std::vector<AspectIface> aspects_;
    std::vector<CapabilityIface> pre_capabilities_;
    std::vector<CapabilityIface> post_capabilities_;
    std::deque<AudioSlice> recent_audio_;
    std::optional<SegmentId> active_segment_id_;
    int sample_rate_ = 16000;
    int channels_ = 1;
    int min_silence_duration_ms_ = 100;
    std::string op_id_ = "vad";
    ProcessingStats* runtime_stats_ = nullptr;

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
            params["__component_name"] = op_id_;
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
            payloads.push_back(aspect.before(runtime, op_id_));
        }
        return payloads;
    }

    void after_aspects(RuntimeContext& runtime, std::vector<std::any> payloads) {
        for (std::size_t i = aspects_.size(); i > 0; --i) {
            aspects_[i - 1].after(runtime, op_id_, std::move(payloads[i - 1]));
        }
    }

    void apply_capabilities(std::vector<CapabilityIface>& capabilities, RuntimeContext& runtime) {
        for (auto& capability : capabilities) {
            capability.apply(runtime);
        }
    }
};

}
