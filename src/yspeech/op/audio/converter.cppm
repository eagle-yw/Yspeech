module;

#include <nlohmann/json.hpp>

export module yspeech.op.audio.converter;

import std;
import yspeech.error;
import yspeech.context;
import yspeech.op;
import yspeech.log;
import yspeech.types;

namespace yspeech {

export enum class AudioFormat {
    Int16,
    Int32,
    Float32,
    Int8
};

export class OpAudioConverter {
public:
    OpAudioConverter() = default;

    OpAudioConverter(const OpAudioConverter&) = delete;
    OpAudioConverter& operator=(const OpAudioConverter&) = delete;
    OpAudioConverter(OpAudioConverter&&) noexcept = default;
    OpAudioConverter& operator=(OpAudioConverter&&) noexcept = default;

    void init(const nlohmann::json& config) {
        try {
            validate_and_load_config(config);
            log_info("OpAudioConverter initialized: input={}, output={}, channels={}, format={}, sample_rate={}",
                     input_buffer_key_, output_buffer_key_, num_channels_,
                     format_to_string(input_format_), default_sample_rate_);
        } catch (const std::exception& e) {
            log_error("OpAudioConverter initialization failed: {}", e.what());
            throw;
        }
    }

    void process(Context& ctx) {
        try {
            if (!output_initialized_) {
                ctx.init_audio_buffer(output_buffer_key_, num_channels_, buffer_capacity_);
                output_initialized_ = true;
            }

            using AudioChunk = std::vector<Byte>;

            AudioChunk raw_chunk;
            if (!ctx.ring_buffer_pop<AudioChunk>(input_buffer_key_, raw_chunk)) {
                ctx.set("audio_eof", true);
                log_debug("No audio data available, setting EOF");
                return;
            }

            int sample_rate = default_sample_rate_;
            int num_channels = num_channels_;

            if (ctx.contains("audio_sample_rate")) {
                try {
                    sample_rate = ctx.get<int>("audio_sample_rate");
                } catch (const std::bad_any_cast& e) {
                    log_warn("Failed to get audio_sample_rate from context: {}", e.what());
                }
            }
            if (ctx.contains("audio_mic_num")) {
                try {
                    num_channels = ctx.get<int>("audio_mic_num");
                } catch (const std::bad_any_cast& e) {
                    log_warn("Failed to get audio_mic_num from context: {}", e.what());
                }
            }

            if (num_channels <= 0) {
                throw std::invalid_argument(std::format("Invalid num_channels: {}", num_channels));
            }

            if (raw_chunk.empty()) {
                log_warn("Empty audio chunk received");
                ctx.set("audio_eof", false);
                return;
            }

            std::size_t sample_size = get_sample_size(input_format_);

            if (raw_chunk.size() % sample_size != 0) {
                log_warn("Audio chunk size {} is not a multiple of sample size {}, truncating",
                         raw_chunk.size(), sample_size);
            }
            std::size_t num_samples = raw_chunk.size() / sample_size;

            if (num_samples == 0) {
                log_warn("No valid samples in audio chunk");
                ctx.set("audio_eof", false);
                return;
            }

            if (num_samples % static_cast<std::size_t>(num_channels) != 0) {
                log_warn("Sample count {} is not a multiple of channels {}, truncating",
                         num_samples, num_channels);
                num_samples = (num_samples / static_cast<std::size_t>(num_channels)) * static_cast<std::size_t>(num_channels);
            }

            std::size_t num_frames = num_samples / static_cast<std::size_t>(num_channels);

            std::vector<float> float_data = convert_to_float(raw_chunk, num_samples);

            bool write_success = ctx.audio_buffer_write_interleaved(
                output_buffer_key_,
                float_data.data(),
                num_frames,
                sample_rate
            );

            if (!write_success) {
                log_warn("Failed to write audio to buffer");
                ctx.record_error("OpAudioConverter", "Failed to write audio buffer",
                                "AudioConverter", ErrorCode::OperatorProcessFailed, ErrorLevel::Warning);
            }

            ctx.set("audio_eof", false);

            log_debug("Converted {} interleaved frames to planar format", num_frames);
        } catch (const std::exception& e) {
            log_error("Audio conversion error: {}", e.what());
            ctx.record_error("OpAudioConverter", e.what(), "AudioConverter",
                            ErrorCode::OperatorProcessFailed, ErrorLevel::Error);
            ctx.set("audio_eof", false);
        }
    }

    void deinit() {
    }

private:
    void validate_and_load_config(const nlohmann::json& config) {
        if (config.contains("input_buffer_key")) {
            input_buffer_key_ = config["input_buffer_key"].get<std::string>();
        }
        if (config.contains("output_buffer_key")) {
            output_buffer_key_ = config["output_buffer_key"].get<std::string>();
        }
        if (config.contains("num_channels")) {
            num_channels_ = config["num_channels"].get<int>();
            if (num_channels_ <= 0) {
                throw std::invalid_argument(std::format("num_channels must be positive, got {}", num_channels_));
            }
            if (num_channels_ > 32) {
                throw std::invalid_argument(std::format("num_channels too large, got {}", num_channels_));
            }
        }
        if (config.contains("buffer_capacity")) {
            buffer_capacity_ = config["buffer_capacity"].get<size_t>();
            if (buffer_capacity_ == 0) {
                throw std::invalid_argument("buffer_capacity must be positive");
            }
            if (buffer_capacity_ > static_cast<std::size_t>(16000) * 600) {
                throw std::invalid_argument("buffer_capacity too large");
            }
        }
        if (config.contains("input_format")) {
            std::string format_str = config["input_format"].get<std::string>();
            input_format_ = string_to_format(format_str);
        }
        if (config.contains("default_sample_rate")) {
            default_sample_rate_ = config["default_sample_rate"].get<int>();
            if (default_sample_rate_ <= 0) {
                throw std::invalid_argument(std::format("default_sample_rate must be positive, got {}", default_sample_rate_));
            }
            if (default_sample_rate_ > 192000) {
                throw std::invalid_argument(std::format("default_sample_rate too large, got {}", default_sample_rate_));
            }
        }
        if (config.contains("normalization_factor")) {
            normalization_factor_ = config["normalization_factor"].get<float>();
            if (normalization_factor_ <= 0.0f) {
                throw std::invalid_argument(std::format("normalization_factor must be positive, got {}", normalization_factor_));
            }
        }
        if (config.contains("normalization_factor_int8")) {
            normalization_factor_int8_ = config["normalization_factor_int8"].get<float>();
            if (normalization_factor_int8_ <= 0.0f) {
                throw std::invalid_argument(std::format("normalization_factor_int8 must be positive, got {}", normalization_factor_int8_));
            }
        }
        if (config.contains("normalization_factor_int32")) {
            normalization_factor_int32_ = config["normalization_factor_int32"].get<float>();
            if (normalization_factor_int32_ <= 0.0f) {
                throw std::invalid_argument(std::format("normalization_factor_int32 must be positive, got {}", normalization_factor_int32_));
            }
        }
    }

    std::string format_to_string(AudioFormat format) const {
        switch (format) {
            case AudioFormat::Int16: return "Int16";
            case AudioFormat::Int32: return "Int32";
            case AudioFormat::Float32: return "Float32";
            case AudioFormat::Int8: return "Int8";
            default: return "Unknown";
        }
    }

    AudioFormat string_to_format(const std::string& str) const {
        if (str == "Int16" || str == "int16" || str == "INT16") return AudioFormat::Int16;
        if (str == "Int32" || str == "int32" || str == "INT32") return AudioFormat::Int32;
        if (str == "Float32" || str == "float32" || str == "FLOAT32") return AudioFormat::Float32;
        if (str == "Int8" || str == "int8" || str == "INT8") return AudioFormat::Int8;
        throw std::invalid_argument(std::format("Invalid audio format: {}", str));
    }

    std::size_t get_sample_size(AudioFormat format) const {
        switch (format) {
            case AudioFormat::Int8: return 1;
            case AudioFormat::Int16: return 2;
            case AudioFormat::Int32: return 4;
            case AudioFormat::Float32: return 4;
            default: return 2;
        }
    }

    std::vector<float> convert_to_float(const std::vector<Byte>& raw_data, std::size_t num_samples) const {
        std::vector<float> result;
        result.reserve(num_samples);

        switch (input_format_) {
            case AudioFormat::Int16: {
                for (std::size_t i = 0; i < num_samples; ++i) {
                    std::size_t offset = i * 2;
                    int16_t sample = 0;
                    std::memcpy(&sample, raw_data.data() + offset, 2);
                    result.push_back(static_cast<float>(sample) / normalization_factor_);
                }
                break;
            }
            case AudioFormat::Int32: {
                for (std::size_t i = 0; i < num_samples; ++i) {
                    std::size_t offset = i * 4;
                    int32_t sample = 0;
                    std::memcpy(&sample, raw_data.data() + offset, 4);
                    result.push_back(static_cast<float>(sample) / normalization_factor_int32_);
                }
                break;
            }
            case AudioFormat::Float32: {
                for (std::size_t i = 0; i < num_samples; ++i) {
                    std::size_t offset = i * 4;
                    float sample = 0.0f;
                    std::memcpy(&sample, raw_data.data() + offset, 4);
                    result.push_back(sample);
                }
                break;
            }
            case AudioFormat::Int8: {
                for (std::size_t i = 0; i < num_samples; ++i) {
                    std::size_t offset = i * 1;
                    int8_t sample = static_cast<int8_t>(raw_data[offset]);
                    result.push_back(static_cast<float>(sample) / normalization_factor_int8_);
                }
                break;
            }
        }

        return result;
    }

    std::string input_buffer_key_ = "audio_buffer";
    std::string output_buffer_key_ = "audio_planar";
    int num_channels_ = 1;
    std::size_t buffer_capacity_ = static_cast<std::size_t>(16000) * 60;
    bool output_initialized_ = false;
    AudioFormat input_format_ = AudioFormat::Int16;
    int default_sample_rate_ = 16000;
    float normalization_factor_ = 32768.0f;
    float normalization_factor_int8_ = 128.0f;
    float normalization_factor_int32_ = 2147483648.0f;
};

namespace {

OperatorRegistrar<OpAudioConverter> registrar("AudioConverter");

}

}
