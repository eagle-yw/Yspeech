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
    double min_time_ms = std::numeric_limits<double>::max();
    double max_time_ms = 0.0;
    double avg_time_ms = 0.0;
    std::size_t call_count = 0;
    std::vector<double> recent_times_ms;
    
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
    
    std::string to_string() const {
        std::string result = std::format(
            "=== Performance Summary ===\n"
            "Chunks: {}, Segments: {}, Results: {}\n"
            "Processing Time: {:.2f}ms, Audio Duration: {:.2f}ms, RTF: {:.3f}\n"
            "Peak Memory: {:.2f}MB, Avg CPU: {:.1f}%",
            audio_chunks_processed,
            speech_segments_detected,
            asr_results_generated,
            total_processing_time_ms,
            audio_duration_ms,
            rtf,
            peak_memory_mb,
            avg_cpu_percent
        );
        
        if (!operator_timings.empty()) {
            result += "\n\nOperator Performance:\n";
            for (const auto& [id, timing] : operator_timings) {
                result += std::format("  {}: total={:.2f}ms, avg={:.2f}ms, calls={}",
                    id, timing.total_time_ms, timing.avg_time_ms, timing.call_count);
                if (!timing.recent_times_ms.empty()) {
                    result += std::format(", p50={:.2f}ms, p95={:.2f}ms, p99={:.2f}ms",
                        timing.p50(), timing.p95(), timing.p99());
                }
                result += "\n";
            }
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
