module;

#include <nlohmann/json.hpp>

export module yspeech.engine_runtime;

import std;
import yspeech.runtime_common;
import yspeech.context;
import yspeech.frame_source;
import yspeech.pipeline_config;
import yspeech.pipeline_manager;
import yspeech.stream_store;
import yspeech.types;
import yspeech.log;
import yspeech.performance_alert;
import yspeech.resource_monitor;
import yspeech.operators;

namespace yspeech {

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
    std::unique_ptr<PipelineManager> pipeline_manager_;
    std::unique_ptr<Context> context_;
    std::shared_ptr<MicSource> default_frame_source_;
    std::shared_ptr<IFrameSource> active_frame_source_;
    bool offline_mode_ = false;

    std::atomic<bool> running_{false};
    ProcessingStats stats_;
    std::chrono::steady_clock::time_point stats_start_time_{};
    std::chrono::steady_clock::time_point last_performance_emit_{};
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
    PerformanceAlerter alerter_;
    std::vector<float> pending_samples_;
    std::int64_t next_push_pts_ms_ = 0;
    std::atomic<bool> has_input_{false};
    std::atomic<bool> stream_finalized_{false};

    void load_config();
    void init_components();
    void init_alerting();
    void store_frame(AudioFramePtr frame);
    void ingest_frame(AudioFramePtr frame);
    void drain_asr_events();
    void drain_vad_events();
    void emit_status(const std::string& status);
    void emit_alert(const std::string& alert_id, const std::string& message);
    void emit_performance_if_due();
    void finalize_stats();
    void merge_operator_timings(ProcessingStats& target) const;
    ProcessingStats build_live_stats_snapshot() const;
    void finalize_stream_if_needed();
};

EngineRuntime::EngineRuntime(const std::string& config_path)
    : config_path_(config_path) {
    load_config();
    init_components();
}

EngineRuntime::EngineRuntime(const nlohmann::json& config)
    : config_(config) {
    init_components();
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

    context_->set("streaming", !offline_mode_);
    emit_status("started");

    ResourceMonitor::start_monitoring(100);

    event_thread_ = std::thread([this]() {
        while (running_) {
            std::unique_lock<std::mutex> lock(event_mutex_);
            event_cv_.wait_for(lock, std::chrono::milliseconds(50), [this]() {
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
                    finalize_stream_if_needed();
                    emit_status("input_eof");
                    break;
                }

                store_frame(frame);
            }
            return;
        }

        while (running_) {
            if (!active_frame_source_) {
                break;
            }

            AudioFramePtr frame;
            if (!active_frame_source_->next(frame) || !frame) {
                finalize_stream_if_needed();
                emit_status("input_eof");
                break;
            }

            ingest_frame(frame);
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
        drain_asr_events();
        drain_vad_events();
        has_pending_event_work_ = true;
        event_cv_.notify_one();
    }
}

void EngineRuntime::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    ResourceMonitor::stop_monitoring();

    if (active_frame_source_) {
        active_frame_source_->stop();
    }

    if (source_thread_.joinable()) {
        source_thread_.join();
    }

    finalize_stream_if_needed();
    drain_asr_events();
    drain_vad_events();

    event_cv_.notify_all();
    if (event_thread_.joinable()) {
        event_thread_.join();
    }

    finalize_stats();
    if (performance_callback_) {
        performance_callback_(stats_);
    }
    alerter_.check(stats_);
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
    merge_operator_timings(merged);
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
        auto runtime = build_runtime_components(config_);
        pipeline_config_ = std::move(runtime.pipeline_config);
        stream_store_ = std::move(runtime.stream_store);
        pipeline_manager_ = std::move(runtime.pipeline_manager);
        context_ = std::move(runtime.context);
        frame_config_ = read_frame_config(config_);
        offline_mode_ = config_.contains("mode") && config_["mode"].is_string() &&
                        config_["mode"].get<std::string>() == "offline";
        if (config_.contains("task") && config_["task"].is_string()) {
            task_ = config_["task"].get<std::string>();
        }
        init_alerting();

        stream_store_->init_audio_ring(frame_config_.audio_frame_key, frame_config_.ring_capacity_frames);
        default_frame_source_ = std::make_shared<MicSource>("stream");
        active_frame_source_ = default_frame_source_;

        log_info("Engine runtime initialized successfully");
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("Failed to initialize engine runtime: {}", e.what()));
    }
}

void EngineRuntime::init_alerting() {
    alerter_.clear_rules();
    alerter_.add_rule(AlertRule::rtf_high(1.0));
    alerter_.add_rule(AlertRule::memory_high(500.0));
    alerter_.set_callback([this](const std::string& alert_id, const std::string& message) {
        emit_alert(alert_id, message);
    });
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
        total_audio_samples_ += frame->samples_per_channel();
    }

    stats_.audio_chunks_processed++;

    if (frame->eos) {
        context_->set("global_eof", true);
    }

}

void EngineRuntime::ingest_frame(AudioFramePtr frame) {
    store_frame(frame);
    if (!frame || !context_) {
        return;
    }

    pipeline_manager_->run_stream(*context_, *stream_store_, false);
    if (frame->eos) {
        stream_finalized_.store(true, std::memory_order_release);
        pipeline_manager_->run_stream(*context_, *stream_store_, true);
    }

    has_pending_event_work_ = true;
    event_cv_.notify_one();
}

void EngineRuntime::finalize_stream_if_needed() {
    if (!context_ || !pipeline_manager_ || !stream_store_) {
        return;
    }
    if (!has_input_.load(std::memory_order_acquire)) {
        return;
    }
    if (stream_finalized_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    context_->set("global_eof", true);
    if (offline_mode_) {
        pipeline_manager_->run_stream(*context_, *stream_store_, false);
    }
    pipeline_manager_->run_stream(*context_, *stream_store_, true);
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
    snapshot.audio_duration_ms =
        static_cast<double>(total_audio_samples_) / static_cast<double>(std::max(frame_config_.sample_rate, 1)) * 1000.0;
    if (snapshot.audio_duration_ms > 0.0) {
        snapshot.rtf = snapshot.total_processing_time_ms / snapshot.audio_duration_ms;
    }

    auto resource = ResourceMonitor::get_peak();
    snapshot.peak_memory_mb = static_cast<double>(resource.peak_memory_mb);
    snapshot.avg_cpu_percent = resource.cpu_percent;
    merge_operator_timings(snapshot);
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
    alerter_.check(snapshot);
}

void EngineRuntime::finalize_stats() {
    auto end_time = std::chrono::steady_clock::now();
    stats_.total_processing_time_ms = std::chrono::duration<double, std::milli>(
        end_time - stats_start_time_).count();
    stats_.audio_duration_ms =
        static_cast<double>(total_audio_samples_) / static_cast<double>(std::max(frame_config_.sample_rate, 1)) * 1000.0;
    if (stats_.audio_duration_ms > 0.0) {
        stats_.rtf = stats_.total_processing_time_ms / stats_.audio_duration_ms;
    }

    auto resource = ResourceMonitor::get_peak();
    stats_.peak_memory_mb = static_cast<double>(resource.peak_memory_mb);
    stats_.avg_cpu_percent = resource.cpu_percent;
    merge_operator_timings(stats_);
}

void EngineRuntime::merge_operator_timings(ProcessingStats& target) const {
    if (!context_) {
        return;
    }
    const auto& ctx_stats = context_->performance_stats();
    for (const auto& [op_id, timing] : ctx_stats.operator_timings) {
        if (target.operator_timings.find(op_id) == target.operator_timings.end()) {
            target.operator_timings[op_id] = timing;
        }
    }
}

}
