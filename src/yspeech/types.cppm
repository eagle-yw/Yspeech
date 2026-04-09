module;

export module yspeech.types;

import std;

namespace yspeech {

export using Bytes = std::vector<std::uint8_t>;
export using BytesView = const std::vector<std::uint8_t>&;
export using Byte = std::uint8_t;
export using Size = std::size_t;

export using i32 = std::int32_t;
export using i64 = std::int64_t;
export using f32 = float;
export using f64 = double;

export template<typename T>
concept NonCopyable = !std::copy_constructible<T> && !std::is_copy_assignable_v<T>;

export template<typename T>
concept Movable = std::move_constructible<T> && std::is_move_assignable_v<T>;

export struct AudioData {
    int sample_rate = 16000;
    int num_channels = 1;
    std::int64_t timestamp_ms = 0;
    
    std::vector<std::vector<float>> channels;
    
    bool empty() const { return channels.empty() || channels[0].empty(); }
    std::size_t num_samples() const { return channels.empty() ? 0 : channels[0].size(); }
    std::size_t total_samples() const { return num_samples() * static_cast<std::size_t>(num_channels); }
};

export struct AudioFrame {
    std::string stream_id;
    std::uint64_t seq = 0;

    int sample_rate = 16000;
    int channels = 1;

    std::int64_t pts_ms = 0;
    std::int64_t dur_ms = 10;

    bool eos = false;
    bool gap = false;

    std::vector<float> samples;

    bool empty() const {
        return samples.empty();
    }

    std::size_t samples_per_channel() const {
        auto ch = std::max(channels, 1);
        return samples.size() / static_cast<std::size_t>(ch);
    }
};

export using AudioFramePtr = std::shared_ptr<const AudioFrame>;

export class AudioFramePool {
public:
    AudioFramePool()
        : impl_(std::make_shared<Impl>()) {
    }

    std::shared_ptr<AudioFrame> acquire(std::size_t sample_capacity = 0) const {
        std::unique_ptr<AudioFrame> frame;
        {
            std::lock_guard lock(impl_->mutex);
            if (!impl_->pool.empty()) {
                frame = std::move(impl_->pool.back());
                impl_->pool.pop_back();
            }
        }

        if (!frame) {
            frame = std::make_unique<AudioFrame>();
        }

        reset_frame(*frame, sample_capacity);

        auto* raw = frame.release();
        return std::shared_ptr<AudioFrame>(raw, [impl = impl_](AudioFrame* ptr) {
            if (!ptr) {
                return;
            }

            reset_frame(*ptr, 0);
            std::lock_guard lock(impl->mutex);
            impl->pool.emplace_back(ptr);
        });
    }

private:
    struct Impl {
        std::mutex mutex;
        std::vector<std::unique_ptr<AudioFrame>> pool;
    };

    static void reset_frame(AudioFrame& frame, std::size_t sample_capacity) {
        frame.stream_id.clear();
        frame.seq = 0;
        frame.sample_rate = 16000;
        frame.channels = 1;
        frame.pts_ms = 0;
        frame.dur_ms = 10;
        frame.eos = false;
        frame.gap = false;
        frame.samples.clear();
        if (sample_capacity > frame.samples.capacity()) {
            frame.samples.reserve(sample_capacity);
        }
    }

    std::shared_ptr<Impl> impl_;
};

export struct AudioBufferConfig {
    int num_channels = 1;
    std::size_t capacity_samples = static_cast<std::size_t>(16000) * 60;
};

export struct WordInfo {
    std::string word;
    float start_time_ms = 0.0f;
    float end_time_ms = 0.0f;
    float confidence = 0.0f;
};

export struct AsrResult {
    std::string text;
    float confidence = 0.0f;
    float start_time_ms = 0.0f;
    float end_time_ms = 0.0f;
    std::vector<WordInfo> words;
    std::string language = "unknown";
    std::string emotion;
};

export struct VadSegment {
    std::int64_t start_ms = 0;
    std::int64_t end_ms = 0;
    float confidence = 0.0f;
};

export enum class AsrResultKind {
    Partial,
    SegmentFinal,
    StreamFinal
};

export struct AsrEvent {
    AsrResultKind kind = AsrResultKind::Partial;
    AsrResult result;
    std::optional<VadSegment> segment;
};

export enum class EngineEventKind {
    ResultPartial,
    ResultSegmentFinal,
    ResultStreamFinal,
    VadStart,
    VadEnd,
    Status,
    Alert
};

export struct EngineEvent {
    EngineEventKind kind = EngineEventKind::Status;
    std::string task = "unknown";
    std::string source_id;
    std::int64_t pts_ms = 0;
    std::optional<AsrResult> asr;
    std::optional<VadSegment> vad_segment;
    std::string status;
    std::string alert_id;
    std::string alert_message;
};

export struct OperatorTiming {
    std::string op_id;
    double total_time_ms = 0.0;
    double active_wall_time_ms = 0.0;
    double min_time_ms = std::numeric_limits<double>::max();
    double max_time_ms = 0.0;
    double avg_time_ms = 0.0;
    std::size_t call_count = 0;
    std::size_t effective_call_count = 0;
    double effective_total_time_ms = 0.0;
    double effective_avg_time_ms = 0.0;
    std::vector<double> recent_times_ms;
    std::vector<double> effective_recent_times_ms;
    std::vector<std::pair<double, double>> active_intervals_ms;
    
    void record(double time_ms, std::size_t history_size = 100) {
        total_time_ms += time_ms;
        min_time_ms = std::min(min_time_ms, time_ms);
        max_time_ms = std::max(max_time_ms, time_ms);
        call_count++;
        avg_time_ms = total_time_ms / call_count;
        
        if (history_size > 0) {
            recent_times_ms.push_back(time_ms);
            if (recent_times_ms.size() > history_size) {
                recent_times_ms.erase(recent_times_ms.begin());
            }
        }
    }

    void record_effective_call() {
        effective_call_count++;
    }

    void record_effective_sample(double time_ms, std::size_t history_size = 100) {
        effective_call_count++;
        effective_total_time_ms += time_ms;
        effective_avg_time_ms = effective_total_time_ms / static_cast<double>(effective_call_count);
        if (history_size > 0) {
            effective_recent_times_ms.push_back(time_ms);
            if (effective_recent_times_ms.size() > history_size) {
                effective_recent_times_ms.erase(effective_recent_times_ms.begin());
            }
        }
    }

    void record_active_window(double start_ms, double end_ms) {
        if (!(end_ms > start_ms)) {
            return;
        }

        active_intervals_ms.emplace_back(start_ms, end_ms);
        std::sort(active_intervals_ms.begin(), active_intervals_ms.end());

        std::vector<std::pair<double, double>> merged;
        merged.reserve(active_intervals_ms.size());

        for (const auto& interval : active_intervals_ms) {
            if (merged.empty() || interval.first > merged.back().second) {
                merged.push_back(interval);
            } else {
                merged.back().second = std::max(merged.back().second, interval.second);
            }
        }

        active_intervals_ms = std::move(merged);
        active_wall_time_ms = 0.0;
        for (const auto& interval : active_intervals_ms) {
            active_wall_time_ms += interval.second - interval.first;
        }
    }
    
    double p50() const {
        if (recent_times_ms.empty()) return 0.0;
        auto sorted = recent_times_ms;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }
    
    double p95() const {
        if (recent_times_ms.empty()) return 0.0;
        auto sorted = recent_times_ms;
        std::sort(sorted.begin(), sorted.end());
        std::size_t idx = static_cast<std::size_t>(sorted.size() * 0.95);
        return sorted[std::min(idx, sorted.size() - 1)];
    }
    
    double p99() const {
        if (recent_times_ms.empty()) return 0.0;
        auto sorted = recent_times_ms;
        std::sort(sorted.begin(), sorted.end());
        std::size_t idx = static_cast<std::size_t>(sorted.size() * 0.99);
        return sorted[std::min(idx, sorted.size() - 1)];
    }
};

export struct ProcessingStats {
    std::size_t audio_chunks_processed = 0;
    std::size_t speech_segments_detected = 0;
    std::size_t asr_results_generated = 0;
    
    double total_processing_time_ms = 0.0;
    double engine_init_time_ms = 0.0;
    double operator_total_time_ms = 0.0;
    double non_operator_time_ms = 0.0;
    double operator_time_percent = 0.0;
    double non_operator_time_percent = 0.0;
    double time_to_first_chunk_ms = 0.0;
    double time_to_first_partial_ms = 0.0;
    double time_to_first_final_ms = 0.0;
    double time_to_first_token_ms = 0.0;
    double drain_after_eof_ms = 0.0;
    double stop_overhead_ms = 0.0;
    double stop_resource_monitor_ms = 0.0;
    double stop_source_join_ms = 0.0;
    double stop_finalize_stream_ms = 0.0;
    double stop_drain_events_ms = 0.0;
    double stop_event_join_ms = 0.0;
    double eof_detected_at_ms = 0.0;
    double eof_status_emitted_at_ms = 0.0;
    double eof_status_delay_ms = 0.0;
    std::size_t event_dispatch_calls = 0;
    double event_dispatch_overhead_ms = 0.0;
    double event_queue_push_time_ms = 0.0;
    double event_callback_time_ms = 0.0;
    double event_dispatch_avg_ms = 0.0;
    double event_queue_push_avg_ms = 0.0;
    double event_callback_avg_ms = 0.0;
    double audio_duration_ms = 0.0;
    double rtf = 0.0;
    
    std::unordered_map<std::string, OperatorTiming> operator_timings;
    
    double peak_memory_mb = 0.0;
    double avg_cpu_percent = 0.0;
    
    void record_operator_time(const std::string& op_id, double time_ms, std::size_t history_size = 100) {
        if (operator_timings.find(op_id) == operator_timings.end()) {
            operator_timings[op_id] = OperatorTiming{.op_id = op_id};
        }
        operator_timings[op_id].record(time_ms, history_size);
    }

    void record_operator_effective_call(const std::string& op_id) {
        if (operator_timings.find(op_id) == operator_timings.end()) {
            operator_timings[op_id] = OperatorTiming{.op_id = op_id};
        }
        operator_timings[op_id].record_effective_call();
    }

    void record_operator_effective_sample(const std::string& op_id, double time_ms, std::size_t history_size = 100) {
        if (operator_timings.find(op_id) == operator_timings.end()) {
            operator_timings[op_id] = OperatorTiming{.op_id = op_id};
        }
        operator_timings[op_id].record_effective_sample(time_ms, history_size);
    }

    void record_operator_active_window(const std::string& op_id, double start_ms, double end_ms) {
        if (operator_timings.find(op_id) == operator_timings.end()) {
            operator_timings[op_id] = OperatorTiming{.op_id = op_id};
        }
        operator_timings[op_id].record_active_window(start_ms, end_ms);
    }
    
    std::string to_string() const {
        std::string result = "=== Performance Summary ===\n";
        result += "┌───────────────────────────┬────────────────┐\n";
        result += std::format("│ {:<25} │ {:>14} │\n", "Metric", "Value");
        result += "├───────────────────────────┼────────────────┤\n";
        result += std::format("│ {:<25} │ {:>14} │\n", "Chunks", audio_chunks_processed);
        result += std::format("│ {:<25} │ {:>14} │\n", "Segments", speech_segments_detected);
        result += std::format("│ {:<25} │ {:>14} │\n", "Results", asr_results_generated);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Engine Init Time", engine_init_time_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Processing Time", total_processing_time_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Operator Time", operator_total_time_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Non-Operator Time", non_operator_time_ms);
        result += std::format("│ {:<25} │ {:>11.2f} %  │\n", "Operator Share", operator_time_percent);
        result += std::format("│ {:<25} │ {:>11.2f} %  │\n", "Non-Operator Share", non_operator_time_percent);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Time To First Chunk", time_to_first_chunk_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Time To First Partial", time_to_first_partial_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Time To First Final", time_to_first_final_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Drain After EOF", drain_after_eof_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Stop Overhead", stop_overhead_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Stop.Monitor", stop_resource_monitor_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Stop.SourceJoin", stop_source_join_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Stop.Finalize", stop_finalize_stream_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Stop.DrainEvents", stop_drain_events_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Stop.EventJoin", stop_event_join_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "EOF Detected At", eof_detected_at_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "EOF Status At", eof_status_emitted_at_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "EOF Status Delay", eof_status_delay_ms);
        result += std::format("│ {:<25} │ {:>14} │\n", "Event Dispatch Calls", event_dispatch_calls);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Event Dispatch Time", event_dispatch_overhead_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Event Queue Push Time", event_queue_push_time_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Event Callback Time", event_callback_time_ms);
        result += std::format("│ {:<25} │ {:>11.4f} ms │\n", "Event Dispatch Avg", event_dispatch_avg_ms);
        result += std::format("│ {:<25} │ {:>11.4f} ms │\n", "Event Queue Push Avg", event_queue_push_avg_ms);
        result += std::format("│ {:<25} │ {:>11.4f} ms │\n", "Event Callback Avg", event_callback_avg_ms);
        result += std::format("│ {:<25} │ {:>11.2f} ms │\n", "Audio Duration", audio_duration_ms);
        result += std::format("│ {:<25} │ {:>14.3f} │\n", "RTF", rtf);
        result += std::format("│ {:<25} │ {:>11.2f} MB │\n", "Peak Memory", peak_memory_mb);
        result += std::format("│ {:<25} │ {:>12.1f} % │\n", "Avg CPU", avg_cpu_percent);
        result += "└───────────────────────────┴────────────────┘\n";
        
        if (!operator_timings.empty()) {
            result += "\nOperator Performance:\n";
            result += "┌─────────────────┬──────────┬──────────┬───────┬───────┬──────────┬──────────┬──────────┬─────────┐\n";
            result += "│ Operator        │ Total    │ Avg      │ Calls │ Exec  │ P50      │ P95      │ P99      │ % Task  │\n";
            result += "├─────────────────┼──────────┼──────────┼───────┼───────┼──────────┼──────────┼──────────┼─────────┤\n";
            
            for (const auto& [id, timing] : operator_timings) {
                const double task_share_basis_ms =
                    timing.active_wall_time_ms > 0.0
                        ? timing.active_wall_time_ms
                        : std::min(timing.total_time_ms, total_processing_time_ms);
                double percent = (total_processing_time_ms > 0.0)
                    ? (task_share_basis_ms / total_processing_time_ms * 100.0)
                    : 0.0;
                
                const bool use_effective_percentiles = !timing.effective_recent_times_ms.empty();
                const double p50 = use_effective_percentiles ? [&]{
                    auto sorted = timing.effective_recent_times_ms;
                    std::sort(sorted.begin(), sorted.end());
                    return sorted[sorted.size() / 2];
                }() : timing.p50();
                const double p95 = use_effective_percentiles ? [&]{
                    auto sorted = timing.effective_recent_times_ms;
                    std::sort(sorted.begin(), sorted.end());
                    std::size_t idx = static_cast<std::size_t>(sorted.size() * 0.95);
                    return sorted[std::min(idx, sorted.size() - 1)];
                }() : timing.p95();
                const double p99 = use_effective_percentiles ? [&]{
                    auto sorted = timing.effective_recent_times_ms;
                    std::sort(sorted.begin(), sorted.end());
                    std::size_t idx = static_cast<std::size_t>(sorted.size() * 0.99);
                    return sorted[std::min(idx, sorted.size() - 1)];
                }() : timing.p99();
                const double display_avg_ms = (timing.effective_call_count > 0 && timing.effective_total_time_ms > 0.0)
                    ? timing.effective_avg_time_ms
                    : timing.avg_time_ms;

                if (!timing.recent_times_ms.empty() || !timing.effective_recent_times_ms.empty()) {
                    result += std::format("│ {:<15} │ {:>6.2f}ms │ {:>6.2f}ms │ {:>5} │ {:>5} │ {:>6.2f}ms │ {:>6.2f}ms │ {:>6.2f}ms │ {:>6.2f}% │\n",
                        id, timing.total_time_ms, display_avg_ms, timing.call_count, timing.effective_call_count,
                        p50, p95, p99, percent);
                } else {
                    result += std::format("│ {:<15} │ {:>6.2f}ms │ {:>6.2f}ms │ {:>5} │ {:>5} │ {:>6} │ {:>6} │ {:>6} │ {:>6.2f}% │\n",
                        id, timing.total_time_ms, timing.avg_time_ms, timing.call_count, timing.effective_call_count,
                        "-", "-", "-", percent);
                }
            }
            
            result += "└─────────────────┴──────────┴──────────┴───────┴───────┴──────────┴──────────┴──────────┴─────────┘\n";
        }
        
        return result;
    }
};

export using ResultCallback = std::function<void(const AsrResult&)>;
export using ResultEventCallback = std::function<void(const AsrEvent&)>;
export using EngineEventCallback = std::function<void(const EngineEvent&)>;
export using VadCallback = std::function<void(bool is_speech, std::int64_t start_ms, std::int64_t end_ms)>;
export using StatusCallback = std::function<void(const std::string& status)>;
export using PerformanceCallback = std::function<void(const ProcessingStats&)>;
export using AlertCallback = std::function<void(const std::string& alert_id, const std::string& message)>;

}
