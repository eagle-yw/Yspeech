module;

#include <nlohmann/json.hpp>

export module yspeech.engine;

import std;
import yspeech.engine_runtime;
import yspeech.frame_source;
import yspeech.log;
import yspeech.runtime_common;
import yspeech.types;

namespace yspeech {

export struct EngineConfigOptions {
    std::optional<std::string> audio_path;  ///< Optional audio file path override for source.type=file.
    std::optional<double> playback_rate;    ///< Optional playback rate override; 0.0 means no pacing.
    std::optional<std::string> log_level;   ///< Optional runtime log level override.
};

export class Engine {
public:
    explicit Engine(const std::string& config_path);
    /**
     * @brief Create an engine from a config path with selective runtime overrides.
     * @param config_path Path to pipeline config JSON.
     * @param options Optional overrides applied on top of config file values.
     */
    Engine(const std::string& config_path, const EngineConfigOptions& options);
    explicit Engine(const nlohmann::json& config);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept = delete;
    Engine& operator=(Engine&&) noexcept = delete;

    void start();
    void finish();
    void stop();
    bool is_running() const;

    void set_frame_source(std::shared_ptr<IFrameSource> source);
    void push_frame(AudioFramePtr frame);
    void push_audio(const std::vector<float>& audio);
    void push_audio(const float* data, size_t size);

    bool input_eof_reached() const;

    bool has_event() const;
    EngineEvent get_event();
    std::vector<EngineEvent> get_all_events();

    void on_event(EngineEventCallback callback);
    void on_status(StatusCallback callback);
    void on_performance(PerformanceCallback callback);
    void on_alert(AlertCallback callback);

    ProcessingStats get_stats() const;
    const nlohmann::json& get_config() const;
    std::string get_config_path() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class Engine::Impl {
public:
    explicit Impl(const std::string& config_path)
        : config_path_(config_path), runtime_(config_path) {
        bind_runtime_callbacks();
    }

    Impl(const std::string& config_path, const EngineConfigOptions& options)
        : config_path_(config_path), runtime_(build_config(config_path, options)) {
        bind_runtime_callbacks();
    }

    explicit Impl(const nlohmann::json& config)
        : runtime_(config) {
        bind_runtime_callbacks();
    }

    void start() { runtime_.start(); }
    void finish() { runtime_.finish(); }
    void stop() { runtime_.stop(); }
    bool is_running() const { return runtime_.is_running(); }
    void set_frame_source(std::shared_ptr<IFrameSource> source) { runtime_.set_frame_source(std::move(source)); }
    void push_frame(AudioFramePtr frame) { runtime_.push_frame(std::move(frame)); }
    void push_audio(const std::vector<float>& audio) { runtime_.push_audio(audio); }
    void push_audio(const float* data, size_t size) { runtime_.push_audio(data, size); }
    bool input_eof_reached() const { return runtime_.input_eof_reached(); }

    bool has_event() const {
        std::lock_guard lock(event_mutex_);
        return !event_queue_.empty();
    }

    EngineEvent get_event() {
        std::lock_guard lock(event_mutex_);
        if (event_queue_.empty()) {
            return {};
        }
        auto event = std::move(event_queue_.front());
        event_queue_.pop();
        return event;
    }

    std::vector<EngineEvent> get_all_events() {
        std::lock_guard lock(event_mutex_);
        std::vector<EngineEvent> events;
        while (!event_queue_.empty()) {
            events.push_back(std::move(event_queue_.front()));
            event_queue_.pop();
        }
        return events;
    }

    void on_event(EngineEventCallback callback) {
        event_callback_ = std::move(callback);
    }

    void on_status(StatusCallback callback) {
        status_callback_ = std::move(callback);
    }

    void on_performance(PerformanceCallback callback) {
        performance_callback_ = std::move(callback);
    }

    void on_alert(AlertCallback callback) {
        alert_callback_ = std::move(callback);
    }

    ProcessingStats get_stats() const {
        return runtime_.get_stats();
    }

    const nlohmann::json& get_config() const {
        return runtime_.get_config();
    }

    std::string get_config_path() const {
        auto runtime_path = runtime_.get_config_path();
        if (!runtime_path.empty()) {
            return runtime_path;
        }
        return config_path_;
    }

private:
    std::string config_path_;
    EngineRuntime runtime_;

    mutable std::mutex event_mutex_;
    std::queue<EngineEvent> event_queue_;

    EngineEventCallback event_callback_;
    StatusCallback status_callback_;
    PerformanceCallback performance_callback_;
    AlertCallback alert_callback_;

    static auto build_config(const std::string& config_path, const EngineConfigOptions& options) -> nlohmann::json {
        auto config = load_runtime_config(config_path);
        if (options.log_level.has_value()) {
            config["log_level"] = *options.log_level;
        }
        if (options.audio_path.has_value()) {
            if (config.contains("source") && config["source"].is_object() &&
                config["source"].contains("path") && config["source"]["path"].is_string()) {
                log_warn(
                    "Overriding source.path from config ({}) with EngineConfigOptions.audio_path ({})",
                    config["source"]["path"].get<std::string>(),
                    *options.audio_path
                );
            }
            apply_file_source_override(config, *options.audio_path, options.playback_rate);
        }
        return config;
    }

    void bind_runtime_callbacks() {
        runtime_.on_asr_event([this](const AsrEvent& event) {
            dispatch_asr_event(event);
        });
        runtime_.on_vad([this](bool is_speech, std::int64_t start_ms, std::int64_t end_ms) {
            dispatch_vad_event(is_speech, start_ms, end_ms);
        });
        runtime_.on_status([this](const std::string& status) {
            dispatch_status(status);
        });
        runtime_.on_performance([this](const ProcessingStats& stats) {
            if (performance_callback_) {
                performance_callback_(stats);
            }
        });
        runtime_.on_alert([this](const std::string& alert_id, const std::string& message) {
            dispatch_alert(alert_id, message);
        });
    }

    void dispatch_asr_event(const AsrEvent& event) {
        if (event.result.text.empty()) {
            return;
        }

        EngineEvent engine_event;
        engine_event.task = runtime_.get_task();
        engine_event.kind = to_engine_event_kind(event.kind);
        engine_event.asr = event.result;
        engine_event.vad_segment = event.segment;
        if (event.segment.has_value()) {
            engine_event.pts_ms = event.segment->start_ms;
        } else {
            engine_event.pts_ms = static_cast<std::int64_t>(event.result.start_time_ms);
        }
        dispatch_event(std::move(engine_event));
    }

    void dispatch_vad_event(bool is_speech, std::int64_t start_ms, std::int64_t end_ms) {
        EngineEvent event;
        event.task = "vad";
        event.kind = is_speech ? EngineEventKind::VadStart : EngineEventKind::VadEnd;
        event.pts_ms = start_ms;
        event.vad_segment = VadSegment{
            .start_ms = start_ms,
            .end_ms = end_ms,
            .confidence = 0.0f
        };
        dispatch_event(std::move(event));
    }

    void dispatch_status(const std::string& status) {
        EngineEvent event;
        event.kind = EngineEventKind::Status;
        event.task = runtime_.get_task();
        event.status = status;
        dispatch_event(std::move(event));
        if (status_callback_) {
            status_callback_(status);
        }
    }

    void dispatch_alert(const std::string& alert_id, const std::string& message) {
        EngineEvent event;
        event.kind = EngineEventKind::Alert;
        event.task = runtime_.get_task();
        event.alert_id = alert_id;
        event.alert_message = message;
        dispatch_event(std::move(event));
        if (alert_callback_) {
            alert_callback_(alert_id, message);
        }
    }

    void dispatch_event(EngineEvent event) {
        {
            std::lock_guard lock(event_mutex_);
            event_queue_.push(event);
        }
        if (event_callback_) {
            event_callback_(event);
        }
    }

    static auto to_engine_event_kind(AsrResultKind kind) -> EngineEventKind {
        switch (kind) {
        case AsrResultKind::Partial:
            return EngineEventKind::ResultPartial;
        case AsrResultKind::SegmentFinal:
            return EngineEventKind::ResultSegmentFinal;
        case AsrResultKind::StreamFinal:
            return EngineEventKind::ResultStreamFinal;
        }
        return EngineEventKind::ResultPartial;
    }
};

Engine::Engine(const std::string& config_path)
    : impl_(std::make_unique<Impl>(config_path)) {}

Engine::Engine(const std::string& config_path, const EngineConfigOptions& options)
    : impl_(std::make_unique<Impl>(config_path, options)) {}

Engine::Engine(const nlohmann::json& config)
    : impl_(std::make_unique<Impl>(config)) {}

Engine::~Engine() = default;

void Engine::start() {
    impl_->start();
}

void Engine::finish() {
    impl_->finish();
}

void Engine::stop() {
    impl_->stop();
}

bool Engine::is_running() const {
    return impl_->is_running();
}

void Engine::set_frame_source(std::shared_ptr<IFrameSource> source) {
    impl_->set_frame_source(std::move(source));
}

void Engine::push_frame(AudioFramePtr frame) {
    impl_->push_frame(std::move(frame));
}

void Engine::push_audio(const std::vector<float>& audio) {
    impl_->push_audio(audio);
}

void Engine::push_audio(const float* data, size_t size) {
    impl_->push_audio(data, size);
}

bool Engine::input_eof_reached() const {
    return impl_->input_eof_reached();
}

bool Engine::has_event() const {
    return impl_->has_event();
}

EngineEvent Engine::get_event() {
    return impl_->get_event();
}

std::vector<EngineEvent> Engine::get_all_events() {
    return impl_->get_all_events();
}

void Engine::on_event(EngineEventCallback callback) {
    impl_->on_event(std::move(callback));
}

void Engine::on_status(StatusCallback callback) {
    impl_->on_status(std::move(callback));
}

void Engine::on_performance(PerformanceCallback callback) {
    impl_->on_performance(std::move(callback));
}

void Engine::on_alert(AlertCallback callback) {
    impl_->on_alert(std::move(callback));
}

ProcessingStats Engine::get_stats() const {
    return impl_->get_stats();
}

const nlohmann::json& Engine::get_config() const {
    return impl_->get_config();
}

std::string Engine::get_config_path() const {
    return impl_->get_config_path();
}

export auto create_engine(const std::string& config_path) -> Engine {
    return Engine(config_path);
}

export auto create_engine(const std::string& config_path, const EngineConfigOptions& options) -> Engine {
    return Engine(config_path, options);
}

export auto create_engine(const nlohmann::json& config) -> Engine {
    return Engine(config);
}

}
