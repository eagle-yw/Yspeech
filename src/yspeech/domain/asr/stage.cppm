module;

#include <nlohmann/json.hpp>

export module yspeech.domain.asr.stage;

import std;
import yspeech.aspect;
import yspeech.aspect.logger;
import yspeech.aspect.timer;
import yspeech.capability;
import yspeech.log;
import yspeech.domain.asr.base;
import yspeech.domain.asr.paraformer;
import yspeech.domain.asr.sensevoice;
import yspeech.runtime.runtime_context;
import yspeech.runtime.segment_state;
import yspeech.runtime.segment_registry;
import yspeech.runtime.token;
import yspeech.types;

namespace yspeech {

export class AsrStage {
public:
    void init(const nlohmann::json& config) {
        config_ = config;
        core_name_ = config.value("model_name", std::string("AsrParaformer"));
        core_pool_size_ = std::max<std::size_t>(
            1,
            config.value("core_pool_size", config.value("__core_pool_size", std::size_t{1}))
        );
        min_feature_frames_ = config.value("min_new_feature_frames", 8);
        min_first_partial_feature_frames_ = config.value(
            "min_first_partial_feature_frames",
            std::max(1, std::min(min_feature_frames_, 4))
        );
        max_decode_feature_frames_ = config.value("max_decode_feature_frames", 0);
        enable_incremental_decode_ = config.value("enable_incremental_decode", true);
        core_id_ = config.value("__core_id", core_id_);
        core_pool_.clear();
        core_pool_.reserve(core_pool_size_);
        for (std::size_t i = 0; i < core_pool_size_; ++i) {
            auto holder = std::make_unique<LineCoreHolder>();
            holder->core = AsrCoreFactory::get_instance().create_core(core_name_);
            holder->core->init(config_);
            core_pool_.push_back(std::move(holder));
        }
        configure_aspects(config);
        configure_capabilities(config);
    }

    void process(PipelineToken& token, RuntimeContext& runtime, SegmentRegistry& registry) {
        token.stream_final = false;
        token.stream_asr_result.reset();

        if (!token.segment_id.has_value()) {
            if (token.eos) {
                maybe_emit_stream_final(token, runtime, registry);
            }
            return;
        }

        auto segment = registry.get(*token.segment_id);
        if (!segment) {
            return;
        }

        StreamSnapshot snapshot;
        bool segment_closed = false;
        bool has_partial_work = false;
        bool should_emit_segment_final = false;
        bool reuse_cached_result = false;
        AsrResult cached_result;
        StreamAsrState stream_state;
        {
            std::lock_guard lock(segment->mutex);
            segment_closed = segment->lifecycle == SegmentLifecycle::Closed;
            if (!segment->feature_ready) {
                return;
            }
        }
        snapshot = collect_stream_snapshot(runtime, token.stream_id);
        if (snapshot.features.empty()) {
            return;
        }
        {
            std::scoped_lock lock(stream_state_mutex_);
            stream_state = stream_states_[token.stream_id];
        }
        if (enable_incremental_decode_ &&
            snapshot.feature_version > stream_state.accepted_feature_version &&
            !snapshot.delta_features.empty()) {
            auto& holder = core_for(token.line_id);
            std::scoped_lock lock(holder.mutex);
            if (!holder.core) {
                return;
            }
            holder.core->accept_features(token.stream_id, snapshot.delta_features);
            std::scoped_lock state_lock(stream_state_mutex_);
            auto& state = stream_states_[token.stream_id];
            state.accepted_feature_version = snapshot.feature_version;
            state.pending_feature_frames += snapshot.delta_feature_count;
            stream_state.accepted_feature_version = snapshot.feature_version;
            stream_state.pending_feature_frames = state.pending_feature_frames;
        }
        const auto effective_feature_count =
            max_decode_feature_frames_ > 0
                ? std::min(snapshot.feature_count, max_decode_feature_frames_)
                : snapshot.feature_count;
        const auto pending_frames = stream_state.pending_feature_frames;
        has_partial_work =
            snapshot.feature_version > stream_state.last_feature_version &&
            (pending_frames >= min_feature_frames_ || stream_state.last_feature_version == 0);
        should_emit_segment_final =
            segment_closed && snapshot.closed_segment_count > stream_state.finalized_segment_count &&
            snapshot.feature_version > stream_state.finalized_segment_feature_version;
        if (snapshot.feature_version <= stream_state.last_feature_version && !should_emit_segment_final) {
            return;
        }
        if (!segment_closed && stream_state.last_feature_count > 0 &&
            pending_frames < min_feature_frames_) {
            return;
        }
        if (!segment_closed && stream_state.last_feature_count == 0 &&
            effective_feature_count < min_first_partial_feature_frames_) {
            return;
        }
        if (!has_partial_work && !should_emit_segment_final) {
            return;
        }
        if (should_emit_segment_final &&
            snapshot.feature_version == stream_state.last_feature_version &&
            stream_state.last_result_cache.has_value()) {
            reuse_cached_result = true;
            cached_result = *stream_state.last_result_cache;
        }

        AsrResult result;
        if (reuse_cached_result) {
            result = cached_result;
        } else {
            auto& holder = core_for(token.line_id);
            std::scoped_lock lock(holder.mutex);
            if (!holder.core) {
                return;
            }
            apply_capabilities(pre_capabilities_, runtime);
            auto aspect_payloads = before_aspects(runtime);
            if (enable_incremental_decode_ && holder.core->supports_incremental()) {
                result = should_emit_segment_final
                    ? holder.core->decode_final(token.stream_id, snapshot.features)
                    : holder.core->decode_partial(token.stream_id, snapshot.features);
            } else {
                result = holder.core->infer(snapshot.features);
            }
            after_aspects(runtime, std::move(aspect_payloads));
            apply_capabilities(post_capabilities_, runtime);
        }

        if (result.text.empty()) {
            return;
        }

        {
            std::lock_guard lock(segment->mutex);
            segment->last_partial = result;
            if (should_emit_segment_final) {
                segment->final_result = result;
            }
            segment->partial_version += 1;
            token.partial_version = segment->partial_version;
        }
        {
            std::scoped_lock lock(stream_state_mutex_);
            auto& state = stream_states_[token.stream_id];
            state.accepted_feature_version = snapshot.feature_version;
            state.last_feature_version = snapshot.feature_version;
            state.last_feature_count = snapshot.feature_count;
            state.pending_feature_frames = 0;
            state.last_result_cache = result;
            if (should_emit_segment_final) {
                state.finalized_segment_count = snapshot.closed_segment_count;
                state.finalized_segment_feature_version = snapshot.feature_version;
            }
        }

        token.asr_result = result;
        if (should_emit_segment_final) {
            if (auto final_segment = take_next_unemitted_closed_segment(registry, token.stream_id)) {
                std::lock_guard lock(final_segment->mutex);
                token.segment_id = final_segment->segment_id;
                token.vad_segment = final_segment->vad_segment;
                final_segment->final_result = result;
            }
            token.kind = PipelineTokenKind::SegmentFinal;
        } else if (!(token.eos || token.kind == PipelineTokenKind::SegmentFinal || segment_closed)) {
            token.kind = PipelineTokenKind::SegmentUpdate;
        }
        log_debug("AsrStage produced text for segment {}: {}", *token.segment_id, result.text);

        if (token.eos) {
            maybe_emit_stream_final(token, runtime, registry);
        }
    }

    void deinit() {
        for (auto& holder : core_pool_) {
            if (!holder || !holder->core) {
                continue;
            }
            holder->core->deinit();
        }
        core_pool_.clear();
        {
            std::scoped_lock lock(stream_state_mutex_);
            stream_states_.clear();
        }
    }

    void bind_stats(ProcessingStats* stats) {
        runtime_stats_ = stats;
        for (auto& holder : core_pool_) {
            if (holder && holder->core) {
                holder->core->bind_stats(stats);
            }
        }
    }

private:
    struct LineCoreHolder {
        std::mutex mutex;
        std::unique_ptr<AsrCoreIface> core;
    };

    struct StreamAsrState {
        std::uint64_t accepted_feature_version = 0;
        std::uint64_t last_feature_version = 0;
        int last_feature_count = 0;
        int pending_feature_frames = 0;
        std::size_t finalized_segment_count = 0;
        std::uint64_t finalized_segment_feature_version = 0;
        std::uint64_t finalized_stream_feature_version = 0;
        std::optional<AsrResult> last_result_cache;
        bool stream_final_emitted = false;
    };

    struct StreamSnapshot {
        FeatureSequenceView features;
        FeatureSequenceView delta_features;
        std::uint64_t feature_version = 0;
        int feature_count = 0;
        int delta_feature_count = 0;
        std::size_t closed_segment_count = 0;
        std::optional<VadSegment> last_segment;
    };

    auto core_for(std::size_t line_id) -> LineCoreHolder& {
        return *core_pool_.at(line_id % core_pool_.size());
    }

    auto collect_stream_snapshot(RuntimeContext& runtime, const std::string& stream_id) const -> StreamSnapshot {
        StreamSnapshot snapshot;
        {
            std::scoped_lock lock(runtime.stream_feature_mutex);
            if (auto it = runtime.stream_feature_snapshots.find(stream_id);
                it != runtime.stream_feature_snapshots.end()) {
                snapshot.features = FeatureSequenceView::from_chunk_list(
                    it->second.chunks,
                    it->second.feature_count
                );
                snapshot.delta_features = FeatureSequenceView::from_chunk_list(
                    it->second.delta_chunks,
                    it->second.delta_feature_count
                );
                snapshot.feature_version = it->second.version;
                snapshot.feature_count = it->second.feature_count;
                snapshot.delta_feature_count = it->second.delta_feature_count;
            }
        }
        {
            std::scoped_lock lock(runtime.stream_segment_mutex);
            if (auto it = runtime.stream_segment_summaries.find(stream_id);
                it != runtime.stream_segment_summaries.end()) {
                snapshot.closed_segment_count = it->second.closed_segment_count;
                snapshot.last_segment = it->second.last_segment;
            }
        }
        return snapshot;
    }

    auto take_next_unemitted_closed_segment(SegmentRegistry& registry, const std::string& stream_id)
        -> std::shared_ptr<SegmentState> {
        auto segments = registry.snapshot_ordered();
        for (const auto& segment : segments) {
            if (!segment) {
                continue;
            }
            std::lock_guard lock(segment->mutex);
            if (segment->stream_id != stream_id) {
                continue;
            }
            if (segment->lifecycle == SegmentLifecycle::Closed && !segment->final_emitted) {
                segment->final_emitted = true;
                return segment;
            }
        }
        return {};
    }

    void maybe_emit_stream_final(PipelineToken& token, RuntimeContext& runtime, SegmentRegistry& registry) {
        auto snapshot = collect_stream_snapshot(runtime, token.stream_id);
        if (snapshot.features.empty()) {
            return;
        }

        StreamAsrState stream_state;
        {
            std::scoped_lock lock(stream_state_mutex_);
            auto& state = stream_states_[token.stream_id];
            if (state.stream_final_emitted || snapshot.feature_version == state.finalized_stream_feature_version) {
                return;
            }
            stream_state = state;
        }

        AsrResult result;
        const bool has_new_context_since_last_decode =
            snapshot.feature_version > stream_state.last_feature_version;
        const bool can_reuse_cached_stream_final =
            !has_new_context_since_last_decode &&
            stream_state.last_result_cache.has_value();
        if (can_reuse_cached_stream_final) {
            result = *stream_state.last_result_cache;
        } else {
            auto& holder = core_for(token.line_id);
            std::scoped_lock lock(holder.mutex);
            if (!holder.core) {
                return;
            }
            apply_capabilities(pre_capabilities_, runtime);
            auto aspect_payloads = before_aspects(runtime);
            result = enable_incremental_decode_ && holder.core->supports_incremental()
                ? holder.core->decode_final(token.stream_id, snapshot.features)
                : holder.core->infer(snapshot.features);
            after_aspects(runtime, std::move(aspect_payloads));
            apply_capabilities(post_capabilities_, runtime);
        }

        if (result.text.empty()) {
            return;
        }

        token.stream_asr_result = result;
        token.stream_final = true;
        if (auto final_segment = take_next_unemitted_closed_segment(registry, token.stream_id)) {
            std::lock_guard lock(final_segment->mutex);
            token.segment_id = final_segment->segment_id;
            token.vad_segment = final_segment->vad_segment;
            token.asr_result = result;
            token.kind = PipelineTokenKind::SegmentFinal;
            final_segment->final_result = result;
            if (runtime.emit_event) {
                runtime.emit_event(EngineEvent{
                    .kind = EngineEventKind::ResultSegmentFinal,
                    .task = runtime.task,
                    .source_id = token.stream_id,
                    .pts_ms = token.pts_end_ms,
                    .asr = result,
                    .vad_segment = final_segment->vad_segment
                });
                token.asr_result.reset();
            }
        }
        if (!token.vad_segment.has_value() && snapshot.last_segment.has_value()) {
            token.vad_segment = snapshot.last_segment;
        }
        {
            std::scoped_lock lock(stream_state_mutex_);
            auto& state = stream_states_[token.stream_id];
            state.accepted_feature_version = snapshot.feature_version;
            state.last_feature_version = snapshot.feature_version;
            state.last_feature_count = snapshot.feature_count;
            state.pending_feature_frames = 0;
            state.last_result_cache = result;
            state.finalized_stream_feature_version = snapshot.feature_version;
            state.stream_final_emitted = true;
        }
        {
            std::scoped_lock lock(runtime.stream_feature_mutex);
            runtime.stream_feature_snapshots.erase(token.stream_id);
        }
        for (auto& holder : core_pool_) {
            if (!holder || !holder->core) {
                continue;
            }
            std::scoped_lock lock(holder->mutex);
            if (enable_incremental_decode_ && holder->core->supports_incremental()) {
                holder->core->reset_stream(token.stream_id);
            }
        }
        registry.erase_closed();
    }

    nlohmann::json config_;
    std::string core_name_ = "AsrParaformer";
    std::size_t core_pool_size_ = 1;
    int min_feature_frames_ = 8;
    int min_first_partial_feature_frames_ = 4;
    int max_decode_feature_frames_ = 0;
    bool enable_incremental_decode_ = true;
    std::vector<std::unique_ptr<LineCoreHolder>> core_pool_;
    std::vector<AspectIface> aspects_;
    std::vector<CapabilityIface> pre_capabilities_;
    std::vector<CapabilityIface> post_capabilities_;
    std::unordered_map<std::string, StreamAsrState> stream_states_;
    std::mutex stream_state_mutex_;
    ProcessingStats* runtime_stats_ = nullptr;
    std::string core_id_ = "asr";

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
