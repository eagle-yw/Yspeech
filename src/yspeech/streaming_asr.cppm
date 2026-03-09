module;

#include <nlohmann/json.hpp>

export module yspeech.streaming_asr;

import std;
import yspeech.context;
import yspeech.pipeline_manager;
import yspeech.stream_controller;
import yspeech.types;
import yspeech.log;
import yspeech.error;
import yspeech.pipeline_config;
import yspeech.ring_buffer;
import yspeech.resource_monitor;
import yspeech.op.silero_vad;
import yspeech.op.feature.kaldi_fbank;
import yspeech.op.asr.paraformer;

namespace yspeech {

export class StreamingAsr {
public:
    explicit StreamingAsr(const std::string& config_path);
    explicit StreamingAsr(const nlohmann::json& config);
    ~StreamingAsr();
    
    StreamingAsr(const StreamingAsr&) = delete;
    StreamingAsr& operator=(const StreamingAsr&) = delete;
    StreamingAsr(StreamingAsr&&) = delete;
    StreamingAsr& operator=(StreamingAsr&&) = delete;
    
    void start();
    void stop();
    bool is_running() const;
    
    void push_audio(const std::vector<float>& audio);
    void push_audio(const float* data, size_t size);
    
    bool has_result() const;
    AsrResult get_result();
    std::vector<AsrResult> get_all_results();
    
    void on_result(ResultCallback callback);
    void on_vad(VadCallback callback);
    void on_status(StatusCallback callback);
    void on_performance(PerformanceCallback callback);
    void on_alert(AlertCallback callback);
    
    bool is_speaking() const;
    float get_confidence() const;
    ProcessingStats get_stats() const;
    
    const nlohmann::json& get_config() const;
    std::string get_config_path() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class StreamingAsr::Impl {
public:
    Impl() = default;
    
    explicit Impl(const std::string& config_path) {
        config_path_ = config_path;
        load_config();
        init_components();
    }
    
    explicit Impl(const nlohmann::json& config) {
        config_ = config;
        init_components();
    }
    
    ~Impl() {
        stop();
    }

    void load_config() {
        if (!std::filesystem::exists(config_path_)) {
            throw std::runtime_error(std::format("Configuration file not found: {}", config_path_));
        }
        
        std::ifstream file(config_path_);
        if (!file.is_open()) {
            throw std::runtime_error(std::format("Failed to open configuration file: {}", config_path_));
        }
        
        try {
            config_ = nlohmann::json::parse(file);
        } catch (const nlohmann::json::parse_error& e) {
            throw std::runtime_error(std::format("JSON parse error: {}", e.what()));
        }
        
        if (config_.contains("log_level")) {
            std::string level_str = config_["log_level"].get<std::string>();
            LogLevel level = LogLevel::Info;
            if (level_str == "debug") level = LogLevel::Debug;
            else if (level_str == "info") level = LogLevel::Info;
            else if (level_str == "warn" || level_str == "warning") level = LogLevel::Warn;
            else if (level_str == "error") level = LogLevel::Error;
            else if (level_str == "none") level = LogLevel::None;
            set_log_level(level);
        }
        
        log_debug("Loaded configuration from: {}", config_path_);
    }
    
    void init_components() {
        try {
            pipeline_config_ = PipelineConfig::from_json(config_);
            pipeline_manager_ = std::make_unique<PipelineManager>();
            pipeline_manager_->build(pipeline_config_);
            
            context_ = std::make_unique<Context>();
            
            init_streaming_mode();
            
            log_info("StreamingAsr initialized successfully");
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("Failed to initialize components: {}", e.what()));
        }
    }
    
    void init_streaming_mode() {
        context_->init_audio_buffer("audio_planar", 1, 16000 * 60);
        
        input_ring_buffer_ = std::make_shared<RingBuffer<float>>(16000 * 30);
        
        stream_controller_ = std::make_unique<AudioStreamController>();
        
        stream_controller_->set_data_source([this](auto& chunks) -> bool {
            using ChunkType = std::remove_reference_t<decltype(chunks)>;
            using ElementType = typename ChunkType::value_type;
            
            constexpr size_t batch_size = 1600;
            std::vector<float> audio_batch(batch_size);
            
            size_t samples_read = input_ring_buffer_->pop_batch_wait(
                audio_batch.data(), batch_size, std::chrono::milliseconds(100));
            
            if (samples_read == 0) {
                if (!running_) {
                    return false;
                }
                return false;
            }
            
            context_->audio_buffer_write_interleaved("audio_planar", 
                audio_batch.data(), samples_read, 16000);
            
            return true;
        });
        
        stream_controller_->set_buffer_key("audio_planar");
        stream_controller_->set_eof_flag("global_eof");
        stream_controller_->set_manager(pipeline_manager_.get());
    }

    void start() {
        if (running_) {
            log_warn("Already running");
            return;
        }
        
        running_ = true;
        stats_start_time_ = std::chrono::steady_clock::now();
        
        context_->set("streaming", true);
        
        ResourceMonitor::start_monitoring(100);
        
        result_thread_ = std::thread([this]() {
            while (running_) {
                std::unique_lock<std::mutex> lock(result_mutex_);
                result_cv_.wait_for(lock, std::chrono::milliseconds(50), [this]() {
                    return !running_ || has_pending_result_;
                });
                
                if (!running_) break;
                
                has_pending_result_ = false;
                lock.unlock();
                
                if (!context_) continue;
                
                if (context_->contains("asr_results")) {
                    try {
                        auto results = context_->get<std::vector<AsrResult>>("asr_results");
                        lock.lock();
                        for (auto& result : results) {
                            if (!result.text.empty()) {
                                result_queue_.push(result);
                                stats_.asr_results_generated++;
                                
                                if (result_callback_) {
                                    lock.unlock();
                                    result_callback_(result);
                                    lock.lock();
                                }
                            }
                        }
                        lock.unlock();
                        context_->set("asr_results", std::vector<AsrResult>{});
                    } catch (...) {
                    }
                }
                
                if (context_->contains("vad_is_speech")) {
                    try {
                        bool is_speech = context_->get<bool>("vad_is_speech");
                        if (is_speech != last_vad_state_) {
                            if (is_speech && !last_vad_state_) {
                                stats_.speech_segments_detected++;
                            }
                            if (vad_callback_) {
                                vad_callback_(is_speech, 0, 0);
                            }
                            last_vad_state_ = is_speech;
                        }
                    } catch (...) {
                    }
                }
            }
        });
        
        stream_controller_->start(*context_);
        
        log_info("Streaming ASR started");
    }
    
    void stop() {
        if (!running_) {
            return;
        }
        
        running_ = false;
        
        ResourceMonitor::stop_monitoring();
        
        if (input_ring_buffer_) {
            input_ring_buffer_->stop();
        }
        
        if (stream_controller_) {
            stream_controller_->stop();
        }
        
        if (pipeline_manager_) {
            pipeline_manager_->stop();
        }
        
        if (result_thread_.joinable()) {
            result_thread_.join();
        }
        
        finalize_stats();
        
        log_info("Streaming ASR stopped");
    }
    
    bool is_running() const {
        return running_;
    }

    void push_audio(const std::vector<float>& audio) {
        if (!running_ || !input_ring_buffer_) {
            return;
        }
        
        input_ring_buffer_->push_batch(audio.data(), audio.size());
        stats_.audio_chunks_processed++;
        total_audio_samples_ += audio.size();
    }
    
    void push_audio(const float* data, size_t size) {
        if (!running_ || !input_ring_buffer_) {
            return;
        }
        
        input_ring_buffer_->push_batch(data, size);
        stats_.audio_chunks_processed++;
        total_audio_samples_ += size;
    }

    bool has_result() const {
        std::lock_guard<std::mutex> lock(result_mutex_);
        return !result_queue_.empty();
    }
    
    AsrResult get_result() {
        std::lock_guard<std::mutex> lock(result_mutex_);
        if (result_queue_.empty()) {
            return AsrResult{};
        }
        
        auto result = std::move(result_queue_.front());
        result_queue_.pop();
        return result;
    }
    
    std::vector<AsrResult> get_all_results() {
        std::lock_guard<std::mutex> lock(result_mutex_);
        std::vector<AsrResult> results;
        while (!result_queue_.empty()) {
            results.push_back(std::move(result_queue_.front()));
            result_queue_.pop();
        }
        return results;
    }

    void on_result(ResultCallback callback) {
        result_callback_ = std::move(callback);
    }
    
    void on_vad(VadCallback callback) {
        vad_callback_ = std::move(callback);
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

    bool is_speaking() const {
        if (context_ && context_->contains("is_speaking")) {
            try {
                return context_->get<bool>("is_speaking");
            } catch (...) {
                return false;
            }
        }
        return false;
    }
    
    float get_confidence() const {
        if (context_ && context_->contains("confidence")) {
            try {
                return context_->get<float>("confidence");
            } catch (...) {
                return 0.0f;
            }
        }
        return 0.0f;
    }
    
    ProcessingStats get_stats() const {
        auto merged = stats_;
        merge_operator_timings(merged);
        return merged;
    }
    
    void finalize_stats() {
        auto end_time = std::chrono::steady_clock::now();
        stats_.total_processing_time_ms = std::chrono::duration<double, std::milli>(
            end_time - stats_start_time_).count();
        
        stats_.audio_duration_ms = static_cast<double>(total_audio_samples_) / 16000.0 * 1000.0;
        
        if (stats_.audio_duration_ms > 0) {
            stats_.rtf = stats_.total_processing_time_ms / stats_.audio_duration_ms;
        }
        
        auto resource = ResourceMonitor::get_peak();
        stats_.peak_memory_mb = static_cast<double>(resource.peak_memory_mb);
        stats_.avg_cpu_percent = resource.cpu_percent;
        
        merge_operator_timings(stats_);
    }
    
    void merge_operator_timings(ProcessingStats& target) const {
        if (context_) {
            const auto& ctx_stats = context_->performance_stats();
            for (const auto& [op_id, timing] : ctx_stats.operator_timings) {
                if (target.operator_timings.find(op_id) == target.operator_timings.end()) {
                    target.operator_timings[op_id] = timing;
                }
            }
        }
    }

    const nlohmann::json& get_config() const {
        return config_;
    }
    
    std::string get_config_path() const {
        return config_path_;
    }

private:
    std::string config_path_;
    nlohmann::json config_;
    PipelineConfig pipeline_config_;
    std::unique_ptr<PipelineManager> pipeline_manager_;
    std::unique_ptr<Context> context_;
    std::unique_ptr<AudioStreamController> stream_controller_;
    std::shared_ptr<RingBuffer<float>> input_ring_buffer_;
    
    std::atomic<bool> running_{false};
    ProcessingStats stats_;
    std::chrono::steady_clock::time_point stats_start_time_;
    std::atomic<size_t> total_audio_samples_{0};
    std::thread result_thread_;
    bool last_vad_state_ = false;
    
    std::queue<AsrResult> result_queue_;
    mutable std::mutex result_mutex_;
    std::condition_variable result_cv_;
    std::atomic<bool> has_pending_result_{false};
    
    ResultCallback result_callback_;
    VadCallback vad_callback_;
    StatusCallback status_callback_;
    PerformanceCallback performance_callback_;
    AlertCallback alert_callback_;
};

StreamingAsr::StreamingAsr(const std::string& config_path)
    : impl_(std::make_unique<Impl>(config_path)) {}

StreamingAsr::StreamingAsr(const nlohmann::json& config)
    : impl_(std::make_unique<Impl>(config)) {}

StreamingAsr::~StreamingAsr() = default;

void StreamingAsr::start() {
    impl_->start();
}

void StreamingAsr::stop() {
    impl_->stop();
}

bool StreamingAsr::is_running() const {
    return impl_->is_running();
}

void StreamingAsr::push_audio(const std::vector<float>& audio) {
    impl_->push_audio(audio);
}

void StreamingAsr::push_audio(const float* data, size_t size) {
    impl_->push_audio(data, size);
}

bool StreamingAsr::has_result() const {
    return impl_->has_result();
}

AsrResult StreamingAsr::get_result() {
    return impl_->get_result();
}

std::vector<AsrResult> StreamingAsr::get_all_results() {
    return impl_->get_all_results();
}

void StreamingAsr::on_result(ResultCallback callback) {
    impl_->on_result(std::move(callback));
}

void StreamingAsr::on_vad(VadCallback callback) {
    impl_->on_vad(std::move(callback));
}

void StreamingAsr::on_status(StatusCallback callback) {
    impl_->on_status(std::move(callback));
}

void StreamingAsr::on_performance(PerformanceCallback callback) {
    impl_->on_performance(std::move(callback));
}

void StreamingAsr::on_alert(AlertCallback callback) {
    impl_->on_alert(std::move(callback));
}

bool StreamingAsr::is_speaking() const {
    return impl_->is_speaking();
}

float StreamingAsr::get_confidence() const {
    return impl_->get_confidence();
}

ProcessingStats StreamingAsr::get_stats() const {
    return impl_->get_stats();
}

const nlohmann::json& StreamingAsr::get_config() const {
    return impl_->get_config();
}

std::string StreamingAsr::get_config_path() const {
    return impl_->get_config_path();
}

export StreamingAsr create_streaming_asr(const std::string& config_path) {
    return StreamingAsr(config_path);
}

export StreamingAsr create_streaming_asr(const nlohmann::json& config) {
    return StreamingAsr(config);
}

} // namespace yspeech
