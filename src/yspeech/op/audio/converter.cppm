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

export class OpAudioConverter {
public:
    OpAudioConverter() = default;

    OpAudioConverter(const OpAudioConverter&) = delete;
    OpAudioConverter& operator=(const OpAudioConverter&) = delete;
    OpAudioConverter(OpAudioConverter&&) noexcept = default;
    OpAudioConverter& operator=(OpAudioConverter&&) noexcept = default;

    void init(const nlohmann::json& config) {
        if (config.contains("input_buffer_key")) {
            input_buffer_key_ = config["input_buffer_key"].get<std::string>();
        }
        if (config.contains("output_buffer_key")) {
            output_buffer_key_ = config["output_buffer_key"].get<std::string>();
        }
        if (config.contains("num_channels")) {
            num_channels_ = config["num_channels"].get<int>();
        }
        if (config.contains("buffer_capacity")) {
            buffer_capacity_ = config["buffer_capacity"].get<size_t>();
        }
        
        log_info("OpAudioConverter initialized: input={}, output={}, channels={}", 
                 input_buffer_key_, output_buffer_key_, num_channels_);
    }

    void process(Context& ctx) {
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
        
        int sample_rate = 16000;
        int num_channels = num_channels_;
        
        if (ctx.contains("audio_sample_rate")) {
            sample_rate = ctx.get<int>("audio_sample_rate");
        }
        if (ctx.contains("audio_mic_num")) {
            num_channels = ctx.get<int>("audio_mic_num");
        }
        
        const int16_t* pcm = reinterpret_cast<const int16_t*>(raw_chunk.data());
        std::size_t num_samples = raw_chunk.size() / sizeof(int16_t);
        std::size_t num_frames = num_samples / static_cast<std::size_t>(num_channels);
        
        std::vector<float> float_data;
        float_data.reserve(num_samples);
        for (std::size_t i = 0; i < num_samples; ++i) { 
            float_data.push_back(static_cast<float>(pcm[i]) / 32768.0f);
        }
        
        ctx.audio_buffer_write_interleaved(
            output_buffer_key_,
            float_data.data(),
            num_frames,
            sample_rate
        );
        
        ctx.set("audio_eof", false);
        
        log_debug("Converted {} interleaved frames to planar format", num_frames);
    }

    void deinit() {
    }

private:
    std::string input_buffer_key_ = "audio_buffer";
    std::string output_buffer_key_ = "audio_planar";
    int num_channels_ = 1;
    std::size_t buffer_capacity_ = static_cast<std::size_t>(16000) * 60;
    bool output_initialized_ = false;
};

namespace {

OperatorRegistrar<OpAudioConverter> registrar("AudioConverter");

}

}
