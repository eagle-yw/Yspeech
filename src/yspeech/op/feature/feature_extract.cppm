module;

#include <nlohmann/json.hpp>

export module yspeech.op.feature.extract;

import std;
import yspeech.context;
import yspeech.op;
import yspeech.log;

namespace yspeech {

export struct FbankConfig {
    int num_mel_bins = 80;
    int frame_length = 400;  // 25ms at 16kHz
    int frame_shift = 160;   // 10ms at 16kHz
    int sample_rate = 16000;
    float preemphasis_coeff = 0.97f;
    bool use_energy = true;
};

export class FeatureExtractor {
public:
    FeatureExtractor() = default;

    void init(const FbankConfig& config) {
        config_ = config;
        init_mel_filterbank();
        log_info("FeatureExtractor initialized: num_mel_bins={}, sample_rate={}",
                 config_.num_mel_bins, config_.sample_rate);
    }

    std::vector<std::vector<float>> extract_fbank(const std::vector<float>& audio) {
        int num_frames = (static_cast<int>(audio.size()) - config_.frame_length) / config_.frame_shift + 1;
        if (num_frames <= 0) {
            return {};
        }

        std::vector<std::vector<float>> features(num_frames, std::vector<float>(config_.num_mel_bins, 0.0f));

        // Pre-emphasis
        std::vector<float> preemph_audio = audio;
        for (size_t i = 1; i < preemph_audio.size(); ++i) {
            preemph_audio[i] -= config_.preemphasis_coeff * preemph_audio[i - 1];
        }

        for (int frame = 0; frame < num_frames; ++frame) {
            int start = frame * config_.frame_shift;

            // Extract frame and apply Hamming window
            std::vector<float> frame_data(config_.frame_length);
            for (int i = 0; i < config_.frame_length && (start + i) < preemph_audio.size(); ++i) {
                float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (config_.frame_length - 1));
                frame_data[i] = preemph_audio[start + i] * window;
            }

            // Compute FFT (simplified - using energy instead)
            std::vector<float> fft_magnitude = compute_fft_magnitude(frame_data);

            // Apply mel filterbank
            for (int mel = 0; mel < config_.num_mel_bins; ++mel) {
                float mel_energy = 0.0f;
                for (size_t bin = 0; bin < fft_magnitude.size(); ++bin) {
                    mel_energy += fft_magnitude[bin] * mel_filterbank_[mel][bin];
                }
                // Log compression
                features[frame][mel] = std::log(std::max(mel_energy, 1e-10f));
            }
        }

        // Cepstral mean normalization (optional)
        normalize_features(features);

        return features;
    }

    std::vector<std::vector<float>> extract_log_mel_spectrogram(const std::vector<float>& audio, int target_frames = 3000) {
        // For Whisper-style models
        auto fbank = extract_fbank(audio);

        // Pad or trim to target frames
        if (static_cast<int>(fbank.size()) < target_frames) {
            fbank.resize(target_frames, std::vector<float>(config_.num_mel_bins, 0.0f));
        } else if (static_cast<int>(fbank.size()) > target_frames) {
            fbank.resize(target_frames);
        }

        // Transpose to [num_mel_bins, num_frames] format
        std::vector<std::vector<float>> mel_spec(config_.num_mel_bins, std::vector<float>(target_frames));
        for (int mel = 0; mel < config_.num_mel_bins; ++mel) {
            for (int frame = 0; frame < target_frames; ++frame) {
                mel_spec[mel][frame] = fbank[frame][mel];
            }
        }

        return mel_spec;
    }

private:
    void init_mel_filterbank() {
        // Simplified mel filterbank initialization
        int fft_size = config_.frame_length;
        mel_filterbank_.resize(config_.num_mel_bins, std::vector<float>(fft_size / 2 + 1, 0.0f));

        float f_min = 0.0f;
        float f_max = static_cast<float>(config_.sample_rate) / 2.0f;

        float mel_min = hz_to_mel(f_min);
        float mel_max = hz_to_mel(f_max);

        std::vector<float> mel_points(config_.num_mel_bins + 2);
        for (int i = 0; i < config_.num_mel_bins + 2; ++i) {
            mel_points[i] = mel_min + (mel_max - mel_min) * i / (config_.num_mel_bins + 1);
        }

        for (int mel = 0; mel < config_.num_mel_bins; ++mel) {
            float left_mel = mel_points[mel];
            float center_mel = mel_points[mel + 1];
            float right_mel = mel_points[mel + 2];

            for (int bin = 0; bin <= fft_size / 2; ++bin) {
                float freq = static_cast<float>(bin) * config_.sample_rate / fft_size;
                float mel_freq = hz_to_mel(freq);

                if (mel_freq >= left_mel && mel_freq <= center_mel) {
                    mel_filterbank_[mel][bin] = (mel_freq - left_mel) / (center_mel - left_mel);
                } else if (mel_freq > center_mel && mel_freq <= right_mel) {
                    mel_filterbank_[mel][bin] = (right_mel - mel_freq) / (right_mel - center_mel);
                }
            }
        }
    }

    float hz_to_mel(float hz) {
        return 2595.0f * std::log10(1.0f + hz / 700.0f);
    }

    std::vector<float> compute_fft_magnitude(const std::vector<float>& frame) {
        // Simplified FFT - returns energy per frequency bin
        int fft_size = frame.size();
        std::vector<float> magnitude(fft_size / 2 + 1, 0.0f);

        // Simple energy-based approximation
        for (int k = 0; k <= fft_size / 2; ++k) {
            float real = 0.0f, imag = 0.0f;
            for (int n = 0; n < fft_size; ++n) {
                float angle = -2.0f * M_PI * k * n / fft_size;
                real += frame[n] * std::cos(angle);
                imag += frame[n] * std::sin(angle);
            }
            magnitude[k] = std::sqrt(real * real + imag * imag);
        }

        return magnitude;
    }

    void normalize_features(std::vector<std::vector<float>>& features) {
        if (features.empty()) return;

        int num_frames = features.size();
        int num_bins = features[0].size();

        // Compute mean per bin
        std::vector<float> means(num_bins, 0.0f);
        for (const auto& frame : features) {
            for (int i = 0; i < num_bins; ++i) {
                means[i] += frame[i];
            }
        }
        for (float& mean : means) {
            mean /= num_frames;
        }

        // Subtract mean
        for (auto& frame : features) {
            for (int i = 0; i < num_bins; ++i) {
                frame[i] -= means[i];
            }
        }
    }

    FbankConfig config_;
    std::vector<std::vector<float>> mel_filterbank_;
};

export class OpFeatureExtract {
public:
    OpFeatureExtract() = default;

    void init(const nlohmann::json& config) {
        FbankConfig fbank_config;

        if (config.contains("num_mel_bins")) {
            fbank_config.num_mel_bins = config["num_mel_bins"].get<int>();
        }
        if (config.contains("frame_length")) {
            fbank_config.frame_length = config["frame_length"].get<int>();
        }
        if (config.contains("frame_shift")) {
            fbank_config.frame_shift = config["frame_shift"].get<int>();
        }
        if (config.contains("sample_rate")) {
            fbank_config.sample_rate = config["sample_rate"].get<int>();
        }
        if (config.contains("input_buffer_key")) {
            input_buffer_key_ = config["input_buffer_key"].get<std::string>();
        }
        if (config.contains("output_key")) {
            output_key_ = config["output_key"].get<std::string>();
        }

        extractor_.init(fbank_config);

        log_info("OpFeatureExtract initialized");
    }

    void process(Context& ctx) {
        auto audio_buffer = ctx.get_audio_buffer(input_buffer_key_);
        if (!audio_buffer || audio_buffer->channels.empty()) {
            log_debug("No audio buffer available");
            return;
        }

        // Collect audio data
        std::vector<float> audio_data;
        float sample;
        while (audio_buffer->channels[0]->pop(sample)) {
            audio_data.push_back(sample);
        }

        if (audio_data.empty()) {
            return;
        }

        // Extract features
        auto features = extractor_.extract_fbank(audio_data);

        // Save to context
        ctx.set(output_key_ + "_features", features);
        ctx.set(output_key_ + "_num_frames", static_cast<int>(features.size()));

        log_debug("Extracted {} frames of features", features.size());
    }

    void deinit() {
        log_info("OpFeatureExtract deinitialized");
    }

private:
    FeatureExtractor extractor_;
    std::string input_buffer_key_ = "audio_planar";
    std::string output_key_ = "feature";
};

namespace {

OperatorRegistrar<OpFeatureExtract> registrar("FeatureExtract");

}

}
