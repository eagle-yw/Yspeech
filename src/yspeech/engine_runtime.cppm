module;

#include <nlohmann/json.hpp>

export module yspeech.engine_runtime;

import std;
import yspeech.runtime.common;
import yspeech.context;
import yspeech.frame_source;
import yspeech.pipeline_config;
import yspeech.runtime.pipeline_builder;
import yspeech.runtime.runtime_dag;
import yspeech.runtime.runtime_dag_executor;
import yspeech.runtime.pipeline_executor;
import yspeech.runtime.pipeline_recipe;
import yspeech.runtime.segment_registry;
import yspeech.runtime.runtime_context;
import yspeech.runtime.token;
import yspeech.runtime.event_stage;
import yspeech.domain.source.stage;
import yspeech.domain.vad.stage;
import yspeech.domain.feature.stage;
import yspeech.domain.asr.stage;
import yspeech.stream_store;
import yspeech.types;
import yspeech.log;
import yspeech.resource_monitor;

namespace yspeech {

namespace {

auto build_stage_capabilities(
    const PipelineConfig& pipeline_config,
    const CoreConfig& core_config
) -> nlohmann::json {
    nlohmann::json merged = nlohmann::json::array();

    const auto append_capabilities = [&](const nlohmann::json& capabilities) {
        if (!capabilities.is_array()) {
            return;
        }
        for (const auto& entry : capabilities) {
            if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string()) {
                continue;
            }
            auto normalized = entry;
            if (!normalized.contains("params") || !normalized["params"].is_object()) {
                normalized["params"] = nlohmann::json::object();
            }
            normalized["params"] = pipeline_config.resolve_capability_params(normalized["params"]);
            merged.push_back(std::move(normalized));
        }
    };

    append_capabilities(pipeline_config.capabilities());
    append_capabilities(core_config.capabilities);
    return merged;
}

}

export class EngineRuntime {
public:
    explicit EngineRuntime(const std::string& config_path);
    explicit EngineRuntime(const nlohmann::json& config);
    ~EngineRuntime();

    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;
    EngineRuntime(EngineRuntime&&) noexcept = delete;
    EngineRuntime& operator=(EngineRuntime&&) noexcept = delete;

    void start();
    void finish();
    void stop();
    bool is_running() const;

    void set_frame_source(std::shared_ptr<IFrameSource> source);
    void push_frame(AudioFramePtr frame);
    void push_audio(const std::vector<float>& audio);
    void push_audio(const float* data, std::size_t size);

    bool input_eof_reached() const;
    bool is_speaking() const;
    float get_confidence() const;

    void on_asr_event(std::function<void(const AsrEvent&)> callback);
    void on_vad(VadCallback callback);
    void on_status(StatusCallback callback);
    void on_performance(PerformanceCallback callback);
    void on_alert(AlertCallback callback);

    ProcessingStats get_stats() const;
    const nlohmann::json& get_config() const;
    std::string get_config_path() const;
    std::string get_task() const;

private:
    std::string config_path_;
    nlohmann::json config_;
    std::string task_ = "asr";
    PipelineConfig pipeline_config_;
    FrameConfig frame_config_;
    std::unique_ptr<StreamStore> stream_store_;
    std::unique_ptr<Context> context_;
    std::shared_ptr<MicSource> default_frame_source_;
    std::shared_ptr<IFrameSource> active_frame_source_;
    bool offline_mode_ = false;
    std::optional<RuntimeContext> pipeline_runtime_context_;
    std::optional<SegmentRegistry> segment_registry_;
    std::unique_ptr<PipelineExecutor> pipeline_executor_;
    std::unique_ptr<RuntimeDagExecutor> runtime_dag_executor_;
    std::optional<SourceStage> source_stage_;
    std::optional<VadStage> vad_stage_;
    std::optional<FeatureStage> feature_stage_;
    std::optional<AsrStage> asr_stage_;
    std::optional<EventStage> event_stage_;
    std::atomic<std::uint64_t> next_pipeline_token_id_{1};

    std::atomic<bool> running_{false};
    ProcessingStats stats_;
    std::chrono::steady_clock::time_point stats_start_time_{};
    std::chrono::steady_clock::time_point last_performance_emit_{};
    std::chrono::steady_clock::time_point first_chunk_time_{};
    std::chrono::steady_clock::time_point first_partial_time_{};
    std::chrono::steady_clock::time_point first_final_time_{};
    std::chrono::steady_clock::time_point first_token_time_{};
    std::chrono::steady_clock::time_point input_eof_time_{};
    std::chrono::steady_clock::time_point input_eof_status_time_{};
    std::chrono::steady_clock::time_point stream_drained_time_{};
    std::atomic<bool> first_chunk_seen_{false};
    std::atomic<bool> first_partial_seen_{false};
    std::atomic<bool> first_final_seen_{false};
    std::atomic<bool> first_token_seen_{false};
    std::atomic<bool> input_eof_seen_{false};
    std::atomic<bool> input_eof_status_seen_{false};
    std::atomic<bool> stream_drained_seen_{false};
    double engine_init_time_ms_ = 0.0;
    double stop_resource_monitor_ms_ = 0.0;
    double stop_source_join_ms_ = 0.0;
    double stop_finalize_stream_ms_ = 0.0;
    double stop_drain_events_ms_ = 0.0;
    double stop_event_join_ms_ = 0.0;
    std::atomic<std::size_t> total_audio_samples_{0};
    std::thread event_thread_;
    std::thread source_thread_;
    bool last_vad_state_ = false;
    std::size_t last_vad_segment_count_ = 0;

    mutable std::mutex event_mutex_;
    std::condition_variable event_cv_;
    std::atomic<bool> has_pending_event_work_{false};

    std::function<void(const AsrEvent&)> asr_event_callback_;
    VadCallback vad_callback_;
    StatusCallback status_callback_;
    PerformanceCallback performance_callback_;
    AlertCallback alert_callback_;
    std::vector<float> pending_samples_;
    std::int64_t next_push_pts_ms_ = 0;
    std::atomic<bool> has_input_{false};
    std::atomic<bool> stream_finalized_{false};
    std::atomic<bool> stream_drained_emitted_{false};

    void load_config();
    void init_components();
    auto create_frame_source_from_config() -> std::shared_ptr<IFrameSource>;
    void store_frame(AudioFramePtr frame);
    void ingest_frame(AudioFramePtr frame);
    void drain_asr_events();
    void drain_vad_events();
    void emit_status(const std::string& status);
    void emit_alert(const std::string& alert_id, const std::string& message);
    void emit_performance_if_due();
    void finalize_stats();
    void merge_core_timings(ProcessingStats& target) const;
    void update_time_breakdown(ProcessingStats& target, std::chrono::steady_clock::time_point now) const;
    void signal_event_work();
    void emit_stream_drained_once();
    ProcessingStats build_live_stats_snapshot() const;
    void finalize_stream_if_needed();
    void await_pipeline_drain_if_needed();
};

EngineRuntime::EngineRuntime(const std::string& config_path)
    : config_path_(config_path) {
    auto init_start = std::chrono::steady_clock::now();
    load_config();
    init_components();
    engine_init_time_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - init_start).count();
}

EngineRuntime::EngineRuntime(const nlohmann::json& config)
    : config_(config) {
    auto init_start = std::chrono::steady_clock::now();
    init_components();
    engine_init_time_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - init_start).count();
}

EngineRuntime::~EngineRuntime() {
    stop();
}

void EngineRuntime::start() {
    if (running_) {
        log_warn("Already running");
        return;
    }

    running_ = true;
    has_input_.store(false, std::memory_order_release);
    stream_finalized_.store(false, std::memory_order_release);
    stats_start_time_ = std::chrono::steady_clock::now();
    last_performance_emit_ = stats_start_time_;
    first_chunk_seen_.store(false, std::memory_order_release);
    first_partial_seen_.store(false, std::memory_order_release);
    first_final_seen_.store(false, std::memory_order_release);
    first_token_seen_.store(false, std::memory_order_release);
    input_eof_seen_.store(false, std::memory_order_release);
    input_eof_status_seen_.store(false, std::memory_order_release);
    stream_drained_seen_.store(false, std::memory_order_release);
    first_chunk_time_ = {};
    first_partial_time_ = {};
    first_final_time_ = {};
    first_token_time_ = {};
    input_eof_time_ = {};
    input_eof_status_time_ = {};
    stream_drained_time_ = {};
    stop_resource_monitor_ms_ = 0.0;
    stop_source_join_ms_ = 0.0;
    stop_finalize_stream_ms_ = 0.0;
    stop_drain_events_ms_ = 0.0;
    stop_event_join_ms_ = 0.0;
    stream_drained_emitted_.store(false, std::memory_order_release);

    if (pipeline_runtime_context_.has_value()) {
        pipeline_runtime_context_->run_start_time = stats_start_time_;
    }

    context_->set("streaming", !offline_mode_);
    emit_status("started");

    if (runtime_dag_executor_) {
        runtime_dag_executor_->start();
    } else if (pipeline_executor_) {
        pipeline_executor_->start();
    }

    ResourceMonitor::start_monitoring(100);

    event_thread_ = std::thread([this]() {
        while (running_) {
            std::unique_lock<std::mutex> lock(event_mutex_);
            event_cv_.wait_for(lock, std::chrono::milliseconds(10), [this]() {
                return !running_ || has_pending_event_work_;
            });

            if (!running_) {
                break;
            }

            has_pending_event_work_ = false;
            lock.unlock();

            if (!context_) {
                continue;
            }

            drain_asr_events();
            drain_vad_events();
            emit_performance_if_due();
        }
    });

    source_thread_ = std::thread([this]() {
        if (offline_mode_) {
            while (running_) {
                if (!active_frame_source_) {
                    break;
                }

                AudioFramePtr frame;
                if (!active_frame_source_->next(frame) || !frame) {
                    emit_status("input_eof");
                    signal_event_work();
                    finalize_stream_if_needed();
                    drain_asr_events();
                    drain_vad_events();
                    emit_stream_drained_once();
                    signal_event_work();
                    break;
                }

                if (frame->eos) {
                    emit_status("input_eof");
                    signal_event_work();
                }
                ingest_frame(frame);
                if (frame->eos) {
                    finalize_stream_if_needed();
                    await_pipeline_drain_if_needed();
                    drain_asr_events();
                    drain_vad_events();
                    emit_stream_drained_once();
                    signal_event_work();
                    break;
                }
            }
            return;
        }

        while (running_) {
            if (!active_frame_source_) {
                break;
            }

            AudioFramePtr frame;
            if (!active_frame_source_->next(frame) || !frame) {
                emit_status("input_eof");
                signal_event_work();
                finalize_stream_if_needed();
                await_pipeline_drain_if_needed();
                drain_asr_events();
                drain_vad_events();
                emit_stream_drained_once();
                signal_event_work();
                break;
            }

            if (frame->eos) {
                emit_status("input_eof");
                signal_event_work();
            }
            ingest_frame(frame);
            if (frame->eos) {
                await_pipeline_drain_if_needed();
                drain_asr_events();
                drain_vad_events();
                emit_stream_drained_once();
                signal_event_work();
                break;
            }
        }
    });

    log_info("Engine runtime started");
}

void EngineRuntime::finish() {
    if (!running_) {
        return;
    }

    const bool using_default_source =
        active_frame_source_ && default_frame_source_ &&
        active_frame_source_.get() == default_frame_source_.get();

    if (using_default_source && active_frame_source_) {
        active_frame_source_->stop();
    }

    if (using_default_source && source_thread_.joinable()) {
        source_thread_.join();
        finalize_stream_if_needed();
        await_pipeline_drain_if_needed();
        drain_asr_events();
        drain_vad_events();
        emit_stream_drained_once();
        signal_event_work();
    }
}

void EngineRuntime::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    event_cv_.notify_all();

    auto phase_start = std::chrono::steady_clock::now();
    ResourceMonitor::stop_monitoring();
    stop_resource_monitor_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - phase_start).count();

    if (active_frame_source_) {
        active_frame_source_->stop();
    }

    phase_start = std::chrono::steady_clock::now();
    if (source_thread_.joinable()) {
        source_thread_.join();
    }
    stop_source_join_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - phase_start).count();

    phase_start = std::chrono::steady_clock::now();
    finalize_stream_if_needed();
    if (runtime_dag_executor_) {
        runtime_dag_executor_->stop();
    } else if (pipeline_executor_) {
        pipeline_executor_->stop();
    }
    stop_finalize_stream_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - phase_start).count();

    phase_start = std::chrono::steady_clock::now();
    drain_asr_events();
    drain_vad_events();
    emit_stream_drained_once();
    stop_drain_events_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - phase_start).count();

    phase_start = std::chrono::steady_clock::now();
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
    stop_event_join_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - phase_start).count();

    if (source_stage_) {
        source_stage_->deinit();
    }
    if (vad_stage_) {
        vad_stage_->deinit();
    }
    if (feature_stage_) {
        feature_stage_->deinit();
    }
    if (asr_stage_) {
        asr_stage_->deinit();
    }
    event_stage_.reset();

    finalize_stats();
    if (performance_callback_) {
        performance_callback_(stats_);
    }
    emit_status("stopped");

    log_info("Engine runtime stopped");
}

bool EngineRuntime::is_running() const {
    return running_;
}

void EngineRuntime::set_frame_source(std::shared_ptr<IFrameSource> source) {
    if (!source) {
        active_frame_source_ = default_frame_source_;
        return;
    }
    active_frame_source_ = std::move(source);
}

void EngineRuntime::push_frame(AudioFramePtr frame) {
    if (!running_ || !default_frame_source_ || !frame) {
        return;
    }
    default_frame_source_->push_frame(std::move(frame));
}

void EngineRuntime::push_audio(const std::vector<float>& audio) {
    if (!running_ || !default_frame_source_) {
        return;
    }
    push_audio(audio.data(), audio.size());
}

void EngineRuntime::push_audio(const float* data, std::size_t size) {
    if (!running_ || !default_frame_source_ || !data || size == 0) {
        return;
    }

    pending_samples_.insert(pending_samples_.end(), data, data + size);

    const std::size_t frame_samples =
        (static_cast<std::size_t>(frame_config_.sample_rate) * static_cast<std::size_t>(frame_config_.dur_ms)) /
        (1000u * static_cast<std::size_t>(std::max(frame_config_.channels, 1)));

    while (pending_samples_.size() >= frame_samples) {
        std::vector<float> frame_samples_buffer(
            pending_samples_.begin(),
            pending_samples_.begin() + static_cast<std::ptrdiff_t>(frame_samples));
        pending_samples_.erase(
            pending_samples_.begin(),
            pending_samples_.begin() + static_cast<std::ptrdiff_t>(frame_samples));

        auto frame = default_frame_source_->make_frame(
            std::move(frame_samples_buffer),
            next_push_pts_ms_,
            frame_config_.dur_ms,
            false,
            false,
            frame_config_.sample_rate,
            frame_config_.channels
        );
        next_push_pts_ms_ += frame_config_.dur_ms;
        default_frame_source_->push_frame(std::move(frame));
    }
}

bool EngineRuntime::input_eof_reached() const {
    return context_ && context_->get_or_default("global_eof", false);
}

bool EngineRuntime::is_speaking() const {
    if (context_ && context_->contains("is_speaking")) {
        try {
            return context_->get<bool>("is_speaking");
        } catch (...) {
            return false;
        }
    }
    return false;
}

float EngineRuntime::get_confidence() const {
    if (context_ && context_->contains("confidence")) {
        try {
            return context_->get<float>("confidence");
        } catch (...) {
            return 0.0f;
        }
    }
    return 0.0f;
}

void EngineRuntime::on_asr_event(std::function<void(const AsrEvent&)> callback) {
    asr_event_callback_ = std::move(callback);
}

void EngineRuntime::on_vad(VadCallback callback) {
    vad_callback_ = std::move(callback);
}

void EngineRuntime::on_status(StatusCallback callback) {
    status_callback_ = std::move(callback);
}

void EngineRuntime::on_performance(PerformanceCallback callback) {
    performance_callback_ = std::move(callback);
}

void EngineRuntime::on_alert(AlertCallback callback) {
    alert_callback_ = std::move(callback);
}

ProcessingStats EngineRuntime::get_stats() const {
    auto merged = stats_;
    merge_core_timings(merged);
    return merged;
}

const nlohmann::json& EngineRuntime::get_config() const {
    return config_;
}

std::string EngineRuntime::get_config_path() const {
    return config_path_;
}

std::string EngineRuntime::get_task() const {
    return task_;
}

void EngineRuntime::load_config() {
    config_ = load_runtime_config(config_path_);
    log_debug("Loaded configuration from: {}", config_path_);
}

void EngineRuntime::init_components() {
    try {
        apply_runtime_log_level(config_);
        pipeline_config_ = PipelineConfig::from_json(config_);
        stream_store_ = std::make_unique<StreamStore>();
        context_ = std::make_unique<Context>();

        frame_config_ = read_frame_config(config_);
        offline_mode_ = config_.contains("mode") && config_["mode"].is_string() &&
                        config_["mode"].get<std::string>() == "offline";
        if (config_.contains("task") && config_["task"].is_string()) {
            task_ = config_["task"].get<std::string>();
        }

        pipeline_runtime_context_.emplace();
        pipeline_runtime_context_->config = config_;
        pipeline_runtime_context_->task = task_;
        pipeline_runtime_context_->stats = &stats_;
        segment_registry_.emplace();

        auto builder_config = make_pipeline_builder_config(pipeline_config_, config_);
        const bool uses_runtime_dag =
            std::ranges::any_of(builder_config.recipe.stages, [](const auto& stage) {
                return !stage.depends_on.empty() || !stage.downstream_ids.empty();
            });

        pipeline_executor_.reset();
        runtime_dag_executor_.reset();
        if (uses_runtime_dag) {
            runtime_dag_executor_ = std::make_unique<RuntimeDagExecutor>();
            runtime_dag_executor_->configure(builder_config, *pipeline_runtime_context_, *segment_registry_);
        } else {
            pipeline_executor_ = std::make_unique<PipelineExecutor>();
            pipeline_executor_->configure(builder_config, *pipeline_runtime_context_, *segment_registry_);
        }

        source_stage_.emplace();
        vad_stage_.emplace();
        feature_stage_.emplace();
        asr_stage_.emplace();
        event_stage_.emplace();

        if (const auto* stage = builder_config.recipe.stage_by_role(PipelineStageRole::Source); stage) {
            nlohmann::json params = nlohmann::json::object();
            if (stage->stage_id == "source_stage") {
                if (config_.contains("source") && config_["source"].is_object()) {
                    params = config_["source"];
                }
                params["__core_id"] = "source";
                params["core_name"] = stage->core_names.empty()
                    ? nlohmann::json("PassThroughSource")
                    : nlohmann::json(stage->core_names.front());
                params["capabilities"] = nlohmann::json::array();
            } else {
                for (const auto& cfg : pipeline_config_.stages()) {
                    if (cfg.id() == stage->stage_id && !cfg.ops().empty()) {
                        params = cfg.ops().front().params.is_null()
                            ? nlohmann::json::object()
                            : cfg.ops().front().params;
                        params["__core_id"] = cfg.ops().front().id;
                        params["core_name"] = cfg.ops().front().name;
                        params["capabilities"] = build_stage_capabilities(pipeline_config_, cfg.ops().front());
                        break;
                    }
                }
            }
            source_stage_->init(params);
        }

        for (const auto& cfg : pipeline_config_.stages()) {
            if (const auto* stage = builder_config.recipe.stage_by_role(PipelineStageRole::Vad);
                stage && cfg.id() == stage->stage_id && !cfg.ops().empty()) {
                auto params = cfg.ops().front().params.is_null() ? nlohmann::json::object() : cfg.ops().front().params;
                params["__core_id"] = cfg.ops().front().id;
                params["core_name"] = cfg.ops().front().name;
                params["capabilities"] = build_stage_capabilities(pipeline_config_, cfg.ops().front());
                vad_stage_->init(params);
                vad_stage_->bind_stats(&stats_);
            }
            if (const auto* stage = builder_config.recipe.stage_by_role(PipelineStageRole::Feature);
                stage && cfg.id() == stage->stage_id && !cfg.ops().empty()) {
                auto params = cfg.ops().front().params.is_null() ? nlohmann::json::object() : cfg.ops().front().params;
                params["__core_id"] = cfg.ops().front().id;
                params["core_name"] = cfg.ops().front().name;
                params["capabilities"] = build_stage_capabilities(pipeline_config_, cfg.ops().front());
                feature_stage_->init(params);
                feature_stage_->bind_stats(&stats_);
            }
            if (const auto* stage = builder_config.recipe.stage_by_role(PipelineStageRole::Asr);
                stage && cfg.id() == stage->stage_id && !cfg.ops().empty()) {
                auto params = cfg.ops().front().params.is_null() ? nlohmann::json::object() : cfg.ops().front().params;
                params["__core_id"] = cfg.ops().front().id;
                params["__core_pool_size"] = builder_config.num_lines;
                params["model_name"] = cfg.ops().front().name;
                params["capabilities"] = build_stage_capabilities(pipeline_config_, cfg.ops().front());
                asr_stage_->init(params);
                asr_stage_->bind_stats(&stats_);
            }
        }

        pipeline_runtime_context_->emit_event = [this](const EngineEvent& event) {
            if (event.kind == EngineEventKind::VadEnd && event.vad_segment.has_value()) {
                stats_.speech_segments_detected++;
                if (vad_callback_) {
                    vad_callback_(false, event.vad_segment->start_ms, event.vad_segment->end_ms);
                }
                return;
            }
            if (!event.asr.has_value()) {
                return;
            }
            if (event.kind == EngineEventKind::ResultPartial) {
                if (!first_partial_seen_.exchange(true, std::memory_order_acq_rel)) {
                    first_partial_time_ = std::chrono::steady_clock::now();
                }
            } else if (!first_final_seen_.exchange(true, std::memory_order_acq_rel)) {
                first_final_time_ = std::chrono::steady_clock::now();
            }
            if (!first_token_seen_.exchange(true, std::memory_order_acq_rel)) {
                first_token_time_ = std::chrono::steady_clock::now();
            }
            stats_.asr_results_generated++;
            if (asr_event_callback_) {
                asr_event_callback_(AsrEvent{
                    .kind = event.kind == EngineEventKind::ResultPartial
                        ? AsrResultKind::Partial
                        : event.kind == EngineEventKind::ResultStreamFinal
                            ? AsrResultKind::StreamFinal
                            : AsrResultKind::SegmentFinal,
                    .result = *event.asr,
                    .segment = event.vad_segment
                });
            }
        };
        auto source_callback = [this](PipelineToken& token, RuntimeContext& runtime, SegmentRegistry& registry) {
            if (source_stage_) {
                source_stage_->process(token, runtime, registry);
            }
        };
        auto vad_callback = [this](PipelineToken& token, RuntimeContext& runtime, SegmentRegistry& registry) {
            if (vad_stage_) {
                vad_stage_->process(token, runtime, registry);
            }
        };
        auto feature_callback = [this](PipelineToken& token, RuntimeContext& runtime, SegmentRegistry& registry) {
            if (feature_stage_) {
                feature_stage_->process(token, runtime, registry);
            }
        };
        auto asr_callback = [this](PipelineToken& token, RuntimeContext& runtime, SegmentRegistry& registry) {
            if (asr_stage_) {
                asr_stage_->process(token, runtime, registry);
            }
        };
        auto event_callback = [this](PipelineToken& token, RuntimeContext& runtime, SegmentRegistry& registry) {
            if (event_stage_) {
                event_stage_->process(token, runtime, registry);
            }
        };

        if (runtime_dag_executor_) {
            runtime_dag_executor_->set_stage_callback(PipelineStageRole::Source, source_callback);
            runtime_dag_executor_->set_stage_callback(PipelineStageRole::Vad, vad_callback);
            runtime_dag_executor_->set_stage_callback(PipelineStageRole::Feature, feature_callback);
            runtime_dag_executor_->set_stage_callback(PipelineStageRole::Asr, asr_callback);
            runtime_dag_executor_->set_stage_callback(PipelineStageRole::Event, event_callback);
        } else if (pipeline_executor_) {
            pipeline_executor_->set_source_stage(source_callback);
            pipeline_executor_->set_vad_stage(vad_callback);
            pipeline_executor_->set_feature_stage(feature_callback);
            pipeline_executor_->set_asr_stage(asr_callback);
            pipeline_executor_->set_event_stage(event_callback);
        }
        stream_store_->init_audio_ring(frame_config_.audio_frame_key, frame_config_.ring_capacity_frames);
        std::string source_type = "microphone";
        if (config_.contains("source") && config_["source"].is_object() &&
            config_["source"].contains("type") && config_["source"]["type"].is_string()) {
            source_type = config_["source"]["type"].get<std::string>();
        }
        if (source_type == "stream") {
            default_frame_source_ = std::make_shared<StreamSource>("stream");
        } else {
            default_frame_source_ = std::make_shared<MicSource>("stream");
        }
        
        auto configured_source = create_frame_source_from_config();
        if (configured_source) {
            active_frame_source_ = configured_source;
            log_info("Using configured frame source");
        } else {
            active_frame_source_ = default_frame_source_;
            log_info("Using default microphone frame source");
        }

        log_info("Engine runtime initialized successfully");
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("Failed to initialize engine runtime: {}", e.what()));
    }
}

auto EngineRuntime::create_frame_source_from_config() -> std::shared_ptr<IFrameSource> {
    if (!config_.contains("source") || !config_["source"].is_object()) {
        return nullptr;
    }

    const auto& source_cfg = config_["source"];
    if (!source_cfg.contains("type") || !source_cfg["type"].is_string()) {
        log_warn("Source config missing 'type' field, using default");
        return nullptr;
    }

    std::string type = source_cfg["type"].get<std::string>();
    
    if (type == "file") {
        if (!source_cfg.contains("path") || !source_cfg["path"].is_string()) {
            log_warn("File source config missing 'path' field");
            return nullptr;
        }
        
        std::string path = source_cfg["path"].get<std::string>();
        double playback_rate = source_cfg.value("playback_rate", 1.0);
        const double effective_playback_rate = offline_mode_ ? 0.0 : playback_rate;
        
        log_info("Creating FileSource: path={}, playback_rate={}", 
                 path, effective_playback_rate);
        
        auto file_source = std::make_shared<FileSource>(path, "file", effective_playback_rate);
        return std::make_shared<AudioFramePipelineSource>(file_source);
    }
    
    if (type == "microphone") {
        log_info("Using dedicated microphone source");
        return default_frame_source_;
    }
    
    if (type == "stream") {
        log_info("Using dedicated stream source");
        return default_frame_source_;
    }
    
    log_warn("Unknown source type: {}", type);
    return nullptr;
}

void EngineRuntime::store_frame(AudioFramePtr frame) {
    if (!frame || !context_) {
        return;
    }

    context_->set("audio_frame", frame);
    context_->set("audio_frame_stream_id", frame->stream_id);
    context_->set("audio_frame_seq", frame->seq);
    context_->set("audio_frame_pts_ms", frame->pts_ms);
    context_->set("audio_frame_dur_ms", frame->dur_ms);
    context_->set("audio_frame_gap", frame->gap);
    context_->set("audio_frame_eos", frame->eos);
    stream_store_->push_frame(frame_config_.audio_frame_key, frame);
    has_input_.store(true, std::memory_order_release);

    if (!frame->samples.empty()) {
        if (!first_chunk_seen_.exchange(true, std::memory_order_acq_rel)) {
            first_chunk_time_ = std::chrono::steady_clock::now();
        }
        total_audio_samples_ += frame->samples_per_channel();
    }

    stats_.audio_chunks_processed++;

    if (frame->eos) {
        context_->set("global_eof", true);
        if (!input_eof_seen_.exchange(true, std::memory_order_acq_rel)) {
            input_eof_time_ = std::chrono::steady_clock::now();
        }
    }

}

void EngineRuntime::ingest_frame(AudioFramePtr frame) {
    store_frame(frame);
    if (!frame || !context_) {
        return;
    }

    PipelineToken token;
    token.token_id = next_pipeline_token_id_.fetch_add(1, std::memory_order_acq_rel);
    token.stream_id = frame->stream_id;
    token.pts_begin_ms = frame->pts_ms;
    token.pts_end_ms = frame->pts_ms + frame->dur_ms;
    token.eos = frame->eos;
    token.kind = frame->eos ? PipelineTokenKind::EndOfStream : PipelineTokenKind::AudioWindow;
    token.audio = frame->samples;
    if (runtime_dag_executor_) {
        runtime_dag_executor_->push(std::move(token));
    } else if (pipeline_executor_) {
        pipeline_executor_->push(std::move(token));
    }
    if (frame->eos) {
        stream_finalized_.store(true, std::memory_order_release);
        if (runtime_dag_executor_) {
            runtime_dag_executor_->finish();
        } else if (pipeline_executor_) {
            pipeline_executor_->finish();
        }
    }

    signal_event_work();
}

void EngineRuntime::finalize_stream_if_needed() {
    if (!context_) {
        return;
    }
    if (!has_input_.load(std::memory_order_acquire)) {
        return;
    }
    if (stream_finalized_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    context_->set("global_eof", true);
    if (!input_eof_seen_.exchange(true, std::memory_order_acq_rel)) {
        input_eof_time_ = std::chrono::steady_clock::now();
    }
    if (runtime_dag_executor_) {
        runtime_dag_executor_->finish();
    } else if (pipeline_executor_) {
        pipeline_executor_->finish();
    }
    signal_event_work();
}

void EngineRuntime::await_pipeline_drain_if_needed() {
    if (runtime_dag_executor_) {
        runtime_dag_executor_->wait();
        return;
    }
    if (pipeline_executor_) {
        pipeline_executor_->wait();
    }
}

void EngineRuntime::drain_asr_events() {
    if (!context_ || !context_->contains("asr_events")) {
        return;
    }

    try {
        auto events = context_->get<std::vector<AsrEvent>>("asr_events");
        for (const auto& event : events) {
            if (event.result.text.empty()) {
                continue;
            }
            if (event.kind == AsrResultKind::Partial) {
                if (!first_partial_seen_.exchange(true, std::memory_order_acq_rel)) {
                    first_partial_time_ = std::chrono::steady_clock::now();
                }
            }
            if (event.kind == AsrResultKind::SegmentFinal || event.kind == AsrResultKind::StreamFinal) {
                if (!first_final_seen_.exchange(true, std::memory_order_acq_rel)) {
                    first_final_time_ = std::chrono::steady_clock::now();
                }
            }
            if (!first_token_seen_.exchange(true, std::memory_order_acq_rel)) {
                first_token_time_ = std::chrono::steady_clock::now();
            }
            stats_.asr_results_generated++;
            if (asr_event_callback_) {
                asr_event_callback_(event);
            }
        }
        context_->set("asr_events", std::vector<AsrEvent>{});
    } catch (...) {
    }
}

void EngineRuntime::drain_vad_events() {
    if (!context_) {
        return;
    }

    if (context_->contains("vad_segments")) {
        try {
            auto segments = context_->get<std::vector<VadSegment>>("vad_segments");
            if (segments.size() > last_vad_segment_count_) {
                for (std::size_t i = last_vad_segment_count_; i < segments.size(); ++i) {
                    if (vad_callback_) {
                        const auto& segment = segments[i];
                        vad_callback_(false, segment.start_ms, segment.end_ms);
                    }
                }
                last_vad_segment_count_ = segments.size();
            }
        } catch (...) {
        }
    }

    if (context_->contains("vad_is_speech")) {
        try {
            bool is_speech = context_->get<bool>("vad_is_speech");
            if (is_speech != last_vad_state_) {
                if (is_speech && !last_vad_state_) {
                    stats_.speech_segments_detected++;
                    const auto start_ms = context_->get_or_default<std::int64_t>("vad_current_start_ms", 0);
                    if (vad_callback_) {
                        vad_callback_(true, start_ms, start_ms);
                    }
                }
                last_vad_state_ = is_speech;
            }
        } catch (...) {
        }
    }
}

void EngineRuntime::emit_status(const std::string& status) {
    if (status == "input_eof") {
        if (!input_eof_status_seen_.exchange(true, std::memory_order_acq_rel)) {
            input_eof_status_time_ = std::chrono::steady_clock::now();
        }
    }
    if (status_callback_) {
        status_callback_(status);
    }
}

void EngineRuntime::emit_alert(const std::string& alert_id, const std::string& message) {
    if (alert_callback_) {
        alert_callback_(alert_id, message);
    }
}

ProcessingStats EngineRuntime::build_live_stats_snapshot() const {
    auto snapshot = stats_;
    auto now = std::chrono::steady_clock::now();
    snapshot.total_processing_time_ms = std::chrono::duration<double, std::milli>(
        now - stats_start_time_).count();
    snapshot.engine_init_time_ms = engine_init_time_ms_;
    snapshot.audio_duration_ms =
        static_cast<double>(total_audio_samples_) / static_cast<double>(std::max(frame_config_.sample_rate, 1)) * 1000.0;
    if (snapshot.audio_duration_ms > 0.0) {
        snapshot.rtf = snapshot.total_processing_time_ms / snapshot.audio_duration_ms;
    }

    auto resource = ResourceMonitor::get_peak();
    snapshot.peak_memory_mb = static_cast<double>(resource.peak_memory_mb);
    snapshot.avg_cpu_percent = resource.cpu_percent;
    merge_core_timings(snapshot);
    update_time_breakdown(snapshot, now);
    return snapshot;
}

void EngineRuntime::emit_performance_if_due() {
    if (!performance_callback_ && !alert_callback_) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    if ((now - last_performance_emit_) < std::chrono::seconds(1)) {
        return;
    }
    last_performance_emit_ = now;

    auto snapshot = build_live_stats_snapshot();
    if (performance_callback_) {
        performance_callback_(snapshot);
    }
}

void EngineRuntime::finalize_stats() {
    auto end_time = std::chrono::steady_clock::now();
    stats_.total_processing_time_ms = std::chrono::duration<double, std::milli>(
        end_time - stats_start_time_).count();
    stats_.engine_init_time_ms = engine_init_time_ms_;
    stats_.stop_resource_monitor_ms = stop_resource_monitor_ms_;
    stats_.stop_source_join_ms = stop_source_join_ms_;
    stats_.stop_finalize_stream_ms = stop_finalize_stream_ms_;
    stats_.stop_drain_events_ms = stop_drain_events_ms_;
    stats_.stop_event_join_ms = stop_event_join_ms_;
    stats_.stop_overhead_ms =
        stop_resource_monitor_ms_ +
        stop_source_join_ms_ +
        stop_finalize_stream_ms_ +
        stop_drain_events_ms_ +
        stop_event_join_ms_;
    stats_.audio_duration_ms =
        static_cast<double>(total_audio_samples_) / static_cast<double>(std::max(frame_config_.sample_rate, 1)) * 1000.0;
    if (stats_.audio_duration_ms > 0.0) {
        stats_.rtf = stats_.total_processing_time_ms / stats_.audio_duration_ms;
    }

    auto resource = ResourceMonitor::get_peak();
    stats_.peak_memory_mb = static_cast<double>(resource.peak_memory_mb);
    stats_.avg_cpu_percent = resource.cpu_percent;
    merge_core_timings(stats_);
    update_time_breakdown(stats_, end_time);
}

void EngineRuntime::merge_core_timings(ProcessingStats& target) const {
    if (!context_) {
        return;
    }
    const auto& ctx_stats = context_->performance_stats();
    for (const auto& [op_id, timing] : ctx_stats.core_timings) {
        if (target.core_timings.find(op_id) == target.core_timings.end()) {
            target.core_timings[op_id] = timing;
        }
    }
}

void EngineRuntime::signal_event_work() {
    if (!has_pending_event_work_.exchange(true, std::memory_order_acq_rel)) {
        event_cv_.notify_one();
    }
}

void EngineRuntime::emit_stream_drained_once() {
    if (!stream_drained_emitted_.exchange(true, std::memory_order_acq_rel)) {
        stream_drained_time_ = std::chrono::steady_clock::now();
        stream_drained_seen_.store(true, std::memory_order_release);
        emit_status("stream_drained");
    }
}

void EngineRuntime::update_time_breakdown(ProcessingStats& target, std::chrono::steady_clock::time_point now) const {
    double core_total_ms = 0.0;
    std::vector<std::pair<double, double>> core_active_intervals;
    for (const auto& [op_id, timing] : target.core_timings) {
        (void)op_id;
        core_total_ms += timing.total_time_ms;
        core_active_intervals.insert(
            core_active_intervals.end(),
            timing.active_intervals_ms.begin(),
            timing.active_intervals_ms.end()
        );
    }

    target.core_total_time_ms = core_total_ms;

    double core_active_ms = 0.0;
    if (!core_active_intervals.empty()) {
        std::sort(core_active_intervals.begin(), core_active_intervals.end());
        std::vector<std::pair<double, double>> merged;
        merged.reserve(core_active_intervals.size());
        for (const auto& interval : core_active_intervals) {
            if (!(interval.second > interval.first)) {
                continue;
            }
            if (merged.empty() || interval.first > merged.back().second) {
                merged.push_back(interval);
            } else {
                merged.back().second = std::max(merged.back().second, interval.second);
            }
        }
        for (const auto& interval : merged) {
            core_active_ms += interval.second - interval.first;
        }
    } else {
        core_active_ms = std::min(core_total_ms, target.total_processing_time_ms);
    }

    core_active_ms = std::min(core_active_ms, target.total_processing_time_ms);
    target.core_active_time_ms = core_active_ms;
    target.non_core_time_ms = std::max(0.0, target.total_processing_time_ms - core_active_ms);

    if (target.total_processing_time_ms > 0.0) {
        target.core_time_percent = core_active_ms / target.total_processing_time_ms * 100.0;
        target.non_core_time_percent = target.non_core_time_ms / target.total_processing_time_ms * 100.0;
    } else {
        target.core_time_percent = 0.0;
        target.non_core_time_percent = 0.0;
    }

    if (first_chunk_seen_.load(std::memory_order_acquire)) {
        target.time_to_first_chunk_ms = std::max(
            0.0, std::chrono::duration<double, std::milli>(first_chunk_time_ - stats_start_time_).count());
    } else {
        target.time_to_first_chunk_ms = target.total_processing_time_ms;
    }

    if (first_partial_seen_.load(std::memory_order_acquire)) {
        target.time_to_first_partial_ms = std::max(
            0.0, std::chrono::duration<double, std::milli>(first_partial_time_ - stats_start_time_).count());
    } else {
        target.time_to_first_partial_ms = target.total_processing_time_ms;
    }

    if (first_final_seen_.load(std::memory_order_acquire)) {
        target.time_to_first_final_ms = std::max(
            0.0, std::chrono::duration<double, std::milli>(first_final_time_ - stats_start_time_).count());
    } else {
        target.time_to_first_final_ms = target.total_processing_time_ms;
    }

    if (first_token_seen_.load(std::memory_order_acquire)) {
        target.time_to_first_token_ms = std::max(
            0.0, std::chrono::duration<double, std::milli>(first_token_time_ - stats_start_time_).count());
    } else {
        target.time_to_first_token_ms = target.total_processing_time_ms;
    }

    if (input_eof_seen_.load(std::memory_order_acquire)) {
        const auto drain_end =
            stream_drained_seen_.load(std::memory_order_acquire) ? stream_drained_time_ : now;
        target.drain_after_eof_ms = std::max(
            0.0, std::chrono::duration<double, std::milli>(drain_end - input_eof_time_).count());
        target.eof_detected_at_ms = std::max(
            0.0, std::chrono::duration<double, std::milli>(input_eof_time_ - stats_start_time_).count());
    } else {
        target.drain_after_eof_ms = 0.0;
        target.eof_detected_at_ms = 0.0;
    }

    if (input_eof_status_seen_.load(std::memory_order_acquire)) {
        target.eof_status_emitted_at_ms = std::max(
            0.0, std::chrono::duration<double, std::milli>(input_eof_status_time_ - stats_start_time_).count());
        if (input_eof_seen_.load(std::memory_order_acquire)) {
            target.eof_status_delay_ms = std::max(
                0.0, std::chrono::duration<double, std::milli>(input_eof_status_time_ - input_eof_time_).count());
        } else {
            target.eof_status_delay_ms = 0.0;
        }
    } else {
        target.eof_status_emitted_at_ms = 0.0;
        target.eof_status_delay_ms = 0.0;
    }
}

}
