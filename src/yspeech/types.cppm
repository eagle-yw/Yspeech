module;

#include <cstdint>
#include <cstddef>

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
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    float confidence = 0.0f;
};

export struct ProcessingStats {
    size_t audio_chunks_processed = 0;
    size_t speech_segments_detected = 0;
    size_t asr_results_generated = 0;
    double processing_time_ms = 0.0;
    double rtf = 0.0;
    
    std::string to_string() const {
        return std::format(
            "Stats: chunks={}, segments={}, results={}, time={}ms, RTF={:.2f}",
            audio_chunks_processed,
            speech_segments_detected,
            asr_results_generated,
            processing_time_ms,
            rtf
        );
    }
};

export using ResultCallback = std::function<void(const AsrResult&)>;
export using VadCallback = std::function<void(bool is_speech, int64_t start_ms, int64_t end_ms)>;
export using StatusCallback = std::function<void(const std::string& status)>;

}
