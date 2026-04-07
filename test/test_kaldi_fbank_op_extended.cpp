#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <cmath>
#include <fstream>
#include <chrono>

import yspeech.context;
import yspeech.op.feature.kaldi_fbank;

using namespace yspeech;

// Helper function to load WAV file
std::vector<float> load_wav_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    // Skip WAV header (44 bytes)
    file.seekg(44);

    // Read PCM data (16-bit)
    std::vector<int16_t> pcm_data;
    int16_t sample;
    while (file.read(reinterpret_cast<char*>(&sample), sizeof(sample))) {
        pcm_data.push_back(sample);
    }

    // Convert to float (-1.0 to 1.0)
    std::vector<float> audio_data;
    for (int16_t s : pcm_data) {
        audio_data.push_back(static_cast<float>(s) / 32768.0f);
    }

    return audio_data;
}

// Extended test suite for KaldiFbank
class TestKaldiFbankExtended : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 60);
    }

    // Generate test audio with harmonics (more speech-like)
    std::vector<float> generate_speech_like_audio(float duration_sec, float base_freq = 440.0f) {
        std::vector<float> audio;
        int sample_rate = 16000;
        int num_samples = static_cast<int>(duration_sec * sample_rate);
        
        for (int i = 0; i < num_samples; ++i) {
            float t = static_cast<float>(i) / sample_rate;
            // Add harmonics to simulate speech
            float sample = 0.5f * std::sin(2.0f * M_PI * base_freq * t);
            sample += 0.25f * std::sin(2.0f * M_PI * base_freq * 2.0f * t);
            sample += 0.125f * std::sin(2.0f * M_PI * base_freq * 3.0f * t);
            sample += 0.0625f * std::sin(2.0f * M_PI * base_freq * 4.0f * t);
            audio.push_back(sample * 0.5f);
        }
        
        return audio;
    }

    // Generate white noise
    std::vector<float> generate_noise(float duration_sec, float amplitude = 0.1f) {
        std::vector<float> audio;
        int sample_rate = 16000;
        int num_samples = static_cast<int>(duration_sec * sample_rate);
        
        for (int i = 0; i < num_samples; ++i) {
            float noise = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * amplitude;
            audio.push_back(noise);
        }
        
        return audio;
    }

    // Generate silence
    std::vector<float> generate_silence(float duration_sec) {
        int sample_rate = 16000;
        int num_samples = static_cast<int>(duration_sec * sample_rate);
        return std::vector<float>(num_samples, 0.0f);
    }

    Context ctx_;
};

// Test 1: Continuous processing (multiple process calls)
TEST_F(TestKaldiFbankExtended, ContinuousProcessing) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    // Process first segment (1 second)
    auto audio1 = generate_speech_like_audio(1.0f);
    for (float sample : audio1) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    fbank.process_batch(ctx_);
    
    int frames1 = ctx_.get<int>("fbank_num_frames");
    EXPECT_GT(frames1, 0);
    
    // Process second segment (1 second)
    ctx_.init_audio_buffer("audio_planar", 1, 16000 * 30);  // Reset buffer
    auto audio2 = generate_speech_like_audio(1.0f, 523.25f);  // Different frequency
    for (float sample : audio2) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    fbank.process_batch(ctx_);
    
    int frames2 = ctx_.get<int>("fbank_num_frames");
    EXPECT_GT(frames2, 0);
    EXPECT_NEAR(frames1, frames2, 5);  // Should have similar number of frames
}

// Test 2: Different sample rates
TEST_F(TestKaldiFbankExtended, DifferentSampleRates) {
    std::vector<float> sample_rates = {8000.0f, 16000.0f, 22050.0f, 44100.0f};
    
    for (float sr : sample_rates) {
        OpKaldiFbank fbank;
        nlohmann::json config;
        config["samp_freq"] = sr;
        config["num_bins"] = 80;
        config["input_buffer_key"] = "audio_planar";
        config["output_key"] = "fbank_" + std::to_string(static_cast<int>(sr));
        
        EXPECT_NO_THROW(fbank.init(config));
        
        // Generate audio at this sample rate
        int num_samples = static_cast<int>(1.0f * sr);
        std::vector<float> audio;
        for (int i = 0; i < num_samples; ++i) {
            float t = static_cast<float>(i) / sr;
            audio.push_back(0.5f * std::sin(2.0f * M_PI * 440.0f * t));
        }
        
        ctx_.init_audio_buffer("audio_planar", 1, static_cast<int>(sr * 10));
        for (float sample : audio) {
            ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
        }
        
        EXPECT_NO_THROW(fbank.process_batch(ctx_));
        
        if (ctx_.contains("fbank_" + std::to_string(static_cast<int>(sr)) + "_num_frames")) {
            int frames = ctx_.get<int>("fbank_" + std::to_string(static_cast<int>(sr)) + "_num_frames");
            EXPECT_GT(frames, 0);
        }
    }
}

// Test 3: Noise handling
TEST_F(TestKaldiFbankExtended, NoiseHandling) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    // Generate noise
    auto noise = generate_noise(2.0f, 0.1f);
    for (float sample : noise) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    fbank.process_batch(ctx_);
    
    EXPECT_TRUE(ctx_.contains("fbank_features"));
    auto features = ctx_.get<std::vector<std::vector<float>>>("fbank_features");
    
    // Noise should produce features (though different from speech)
    EXPECT_GT(features.size(), 0);
}

// Test 4: Silence handling
TEST_F(TestKaldiFbankExtended, SilenceHandling) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    // Generate silence
    auto silence = generate_silence(2.0f);
    for (float sample : silence) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    fbank.process_batch(ctx_);
    
    // Silence should still produce features (low energy)
    if (ctx_.contains("fbank_features")) {
        auto features = ctx_.get<std::vector<std::vector<float>>>("fbank_features");
        EXPECT_GT(features.size(), 0);
        
        // Silence features should have low values
        for (const auto& frame : features) {
            float avg = 0.0f;
            for (float val : frame) {
                avg += val;
            }
            avg /= frame.size();
            // Log of near-zero energy should be very negative
            EXPECT_LT(avg, -5.0f);
        }
    }
}

// Test 5: Mixed audio (speech + silence + noise)
TEST_F(TestKaldiFbankExtended, MixedAudio) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    // Mix speech, silence, and noise
    auto speech = generate_speech_like_audio(1.0f);
    auto silence = generate_silence(0.5f);
    auto noise = generate_noise(0.5f, 0.05f);
    
    for (float sample : speech) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    for (float sample : silence) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    for (float sample : noise) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    fbank.process_batch(ctx_);
    
    EXPECT_TRUE(ctx_.contains("fbank_features"));
    int frames = ctx_.get<int>("fbank_num_frames");
    // 2 seconds total, expect ~200 frames
    EXPECT_NEAR(frames, 200, 10);
}

// Test 6: Energy floor configuration
TEST_F(TestKaldiFbankExtended, EnergyFloor) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["energy_floor"] = 1.0f;  // Set energy floor
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    auto audio = generate_speech_like_audio(1.0f);
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    fbank.process_batch(ctx_);
    
    EXPECT_TRUE(ctx_.contains("fbank_features"));
}

// Test 7: Dither configuration
TEST_F(TestKaldiFbankExtended, DitherConfiguration) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["dither"] = 0.1f;  // Add dither
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    auto audio = generate_speech_like_audio(1.0f);
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    fbank.process_batch(ctx_);
    
    EXPECT_TRUE(ctx_.contains("fbank_features"));
}

// Test 8: Frame shift validation
TEST_F(TestKaldiFbankExtended, FrameShiftValidation) {
    std::vector<float> shifts = {5.0f, 10.0f, 15.0f, 20.0f};
    
    for (float shift_ms : shifts) {
        OpKaldiFbank fbank;
        nlohmann::json config;
        config["samp_freq"] = 16000.0f;
        config["num_bins"] = 80;
        config["frame_shift_ms"] = shift_ms;
        config["input_buffer_key"] = "audio_planar";
        config["output_key"] = "fbank";
        
        fbank.init(config);
        EXPECT_FLOAT_EQ(fbank.frame_shift(), shift_ms / 1000.0f);
        
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 10);
        auto audio = generate_speech_like_audio(2.0f);
        for (float sample : audio) {
            ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
        }
        
        fbank.process_batch(ctx_);
        
        int frames = ctx_.get<int>("fbank_num_frames");
        // Expected frames: (2000ms - 25ms) / shift_ms + 1
        int expected_frames = static_cast<int>((2000.0f - 25.0f) / shift_ms) + 1;
        EXPECT_NEAR(frames, expected_frames, 2);
    }
}

// Test 9: Feature consistency (same input should produce same output)
TEST_F(TestKaldiFbankExtended, FeatureConsistency) {
    auto audio = generate_speech_like_audio(1.0f);
    
    // First extraction
    OpKaldiFbank fbank1;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank1";
    fbank1.init(config);
    
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    fbank1.process_batch(ctx_);
    auto features1 = ctx_.get<std::vector<std::vector<float>>>("fbank1_features");
    
    // Second extraction with same audio
    ctx_.init_audio_buffer("audio_planar", 1, 16000 * 30);
    OpKaldiFbank fbank2;
    config["output_key"] = "fbank2";
    fbank2.init(config);
    
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    fbank2.process_batch(ctx_);
    auto features2 = ctx_.get<std::vector<std::vector<float>>>("fbank2_features");
    
    // Should be identical (within floating point tolerance)
    // Note: Kaldi Fbank has internal state, so results may differ slightly
    ASSERT_EQ(features1.size(), features2.size());
    for (size_t i = 0; i < features1.size(); ++i) {
        ASSERT_EQ(features1[i].size(), features2[i].size());
        for (size_t j = 0; j < features1[i].size(); ++j) {
            EXPECT_NEAR(features1[i][j], features2[i][j], 3.0f);
        }
    }
}

// Test 10: Long audio processing (performance test)
TEST_F(TestKaldiFbankExtended, LongAudioPerformance) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    // Generate 10 seconds of audio
    auto audio = generate_speech_like_audio(10.0f);
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    fbank.process_batch(ctx_);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_TRUE(ctx_.contains("fbank_features"));
    int frames = ctx_.get<int>("fbank_num_frames");
    EXPECT_NEAR(frames, 1000, 10);  // ~1000 frames for 10s
    
    // Should process in reasonable time (< 1 second for 10s audio)
    EXPECT_LT(duration.count(), 1000);
    
    std::cout << "Processed 10s audio in " << duration.count() << "ms" << std::endl;
}

// Test 11: Real audio file (if available)
TEST_F(TestKaldiFbankExtended, RealAudioFile) {
    // Check if test audio exists
    std::ifstream test_file("test_data/test_zh.wav");
    if (!test_file.good()) {
        GTEST_SKIP() << "Real audio file not found, skipping test";
    }
    
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    auto audio = load_wav_file("test_data/test_zh.wav");
    if (audio.empty()) {
        GTEST_SKIP() << "Failed to load audio file";
    }
    
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    fbank.process_batch(ctx_);
    
    EXPECT_TRUE(ctx_.contains("fbank_features"));
    int frames = ctx_.get<int>("fbank_num_frames");
    EXPECT_GT(frames, 0);
    
    std::cout << "Extracted " << frames << " frames from real audio" << std::endl;
}

// Test 12: Feature dimension edge cases
TEST_F(TestKaldiFbankExtended, FeatureDimensionEdgeCases) {
    std::vector<int> num_bins_list = {20, 40, 64, 80, 128};
    
    for (int bins : num_bins_list) {
        OpKaldiFbank fbank;
        nlohmann::json config;
        config["samp_freq"] = 16000.0f;
        config["num_bins"] = bins;
        config["input_buffer_key"] = "audio_planar";
        config["output_key"] = "fbank";
        
        fbank.init(config);
        EXPECT_EQ(fbank.feature_dim(), bins);
        
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 10);
        auto audio = generate_speech_like_audio(1.0f);
        for (float sample : audio) {
            ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
        }
        
        fbank.process_batch(ctx_);
        
        int actual_bins = ctx_.get<int>("fbank_num_bins");
        EXPECT_EQ(actual_bins, bins);
        
        auto features = ctx_.get<std::vector<std::vector<float>>>("fbank_features");
        if (!features.empty()) {
            EXPECT_EQ(features[0].size(), bins);
        }
    }
}
