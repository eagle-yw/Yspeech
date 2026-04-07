#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <cmath>

import yspeech.context;
import yspeech.op.feature.kaldi_fbank;

using namespace yspeech;

class TestKaldiFbankOp : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 30);
    }

    // Generate test audio (sine wave)
    std::vector<float> generate_test_audio(float duration_sec, float freq = 440.0f) {
        std::vector<float> audio;
        int sample_rate = 16000;
        int num_samples = static_cast<int>(duration_sec * sample_rate);
        
        for (int i = 0; i < num_samples; ++i) {
            float t = static_cast<float>(i) / sample_rate;
            audio.push_back(0.5f * std::sin(2.0f * M_PI * freq * t));
        }
        
        return audio;
    }

    Context ctx_;
};

// Test basic initialization
TEST_F(TestKaldiFbankOp, BasicInit) {
    OpKaldiFbank fbank;
    
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    EXPECT_NO_THROW(fbank.init(config));
    EXPECT_EQ(fbank.feature_dim(), 80);
    EXPECT_FLOAT_EQ(fbank.frame_shift(), 0.01f);  // 10ms
}

// Test feature extraction
TEST_F(TestKaldiFbankOp, ExtractFeatures) {
    OpKaldiFbank fbank;
    
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["frame_length_ms"] = 25.0f;
    config["frame_shift_ms"] = 10.0f;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    // Generate 3 seconds of test audio
    auto audio = generate_test_audio(3.0f);
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    // Extract features
    fbank.process_batch(ctx_);
    
    // Verify output
    EXPECT_TRUE(ctx_.contains("fbank_features"));
    EXPECT_TRUE(ctx_.contains("fbank_num_frames"));
    EXPECT_TRUE(ctx_.contains("fbank_num_bins"));
    
    int num_frames = ctx_.get<int>("fbank_num_frames");
    int num_bins = ctx_.get<int>("fbank_num_bins");
    
    EXPECT_GT(num_frames, 0);
    EXPECT_EQ(num_bins, 80);
    
    // For 3 seconds at 16kHz with 10ms shift, expect ~300 frames
    EXPECT_NEAR(num_frames, 300, 10);
    
    auto features = ctx_.get<std::vector<std::vector<float>>>("fbank_features");
    EXPECT_EQ(features.size(), num_frames);
    EXPECT_EQ(features[0].size(), num_bins);
}

// Test with different configurations
TEST_F(TestKaldiFbankOp, DifferentConfigs) {
    // Test with 40 bins
    {
        OpKaldiFbank fbank;
        nlohmann::json config;
        config["samp_freq"] = 16000.0f;
        config["num_bins"] = 40;
        config["input_buffer_key"] = "audio_planar";
        config["output_key"] = "fbank";
        
        fbank.init(config);
        
        auto audio = generate_test_audio(1.0f);
        for (float sample : audio) {
            ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
        }
        
        fbank.process_batch(ctx_);
        
        int num_bins = ctx_.get<int>("fbank_num_bins");
        EXPECT_EQ(num_bins, 40);
    }
    
    // Test with different frame settings
    {
        OpKaldiFbank fbank;
        nlohmann::json config;
        config["samp_freq"] = 16000.0f;
        config["num_bins"] = 80;
        config["frame_length_ms"] = 20.0f;
        config["frame_shift_ms"] = 5.0f;
        config["input_buffer_key"] = "audio_planar";
        config["output_key"] = "fbank2";
        
        fbank.init(config);
        EXPECT_FLOAT_EQ(fbank.frame_shift(), 0.005f);  // 5ms
    }
}

// Test window types
TEST_F(TestKaldiFbankOp, WindowTypes) {
    std::vector<std::string> window_types = {"povey", "hamming", "hanning"};
    
    for (const auto& window_type : window_types) {
        OpKaldiFbank fbank;
        nlohmann::json config;
        config["samp_freq"] = 16000.0f;
        config["num_bins"] = 80;
        config["window_type"] = window_type;
        config["input_buffer_key"] = "audio_planar";
        config["output_key"] = "fbank_" + window_type;
        
        EXPECT_NO_THROW(fbank.init(config));
        
        auto audio = generate_test_audio(1.0f);
        for (float sample : audio) {
            ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
        }
        
        EXPECT_NO_THROW(fbank.process_batch(ctx_));
    }
}

// Test pre-emphasis
TEST_F(TestKaldiFbankOp, Preemphasis) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["preemph_coeff"] = 0.97f;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    auto audio = generate_test_audio(2.0f);
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    fbank.process_batch(ctx_);
    
    EXPECT_TRUE(ctx_.contains("fbank_features"));
}

// Test DC offset removal
TEST_F(TestKaldiFbankOp, DCOffsetRemoval) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["remove_dc_offset"] = true;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    // Generate audio with DC offset
    auto audio = generate_test_audio(2.0f);
    for (float& sample : audio) {
        sample += 0.5f;  // Add DC offset
    }
    
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    fbank.process_batch(ctx_);
    
    EXPECT_TRUE(ctx_.contains("fbank_features"));
}

// Test empty audio
TEST_F(TestKaldiFbankOp, EmptyAudio) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    // Process without adding audio
    fbank.process_batch(ctx_);
    
    // Should not crash, but no features extracted
    EXPECT_FALSE(ctx_.contains("fbank_features"));
}

// Test very short audio
TEST_F(TestKaldiFbankOp, ShortAudio) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    // Generate only 20ms of audio (less than one frame)
    auto audio = generate_test_audio(0.02f);
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    fbank.process_batch(ctx_);
    
    // Should not crash, but may have no features or very few
}

// Test frequency range
TEST_F(TestKaldiFbankOp, FrequencyRange) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["low_freq"] = 50.0f;
    config["high_freq"] = 7000.0f;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    auto audio = generate_test_audio(2.0f);
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    EXPECT_NO_THROW(fbank.process_batch(ctx_));
}

// Test feature values are reasonable
TEST_F(TestKaldiFbankOp, FeatureValuesReasonable) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    
    fbank.init(config);
    
    auto audio = generate_test_audio(2.0f);
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    
    fbank.process_batch(ctx_);
    
    auto features = ctx_.get<std::vector<std::vector<float>>>("fbank_features");
    
    // Check that features are finite and reasonable
    for (const auto& frame : features) {
        for (float val : frame) {
            EXPECT_TRUE(std::isfinite(val));
            // Log features are typically negative (log of small numbers)
            EXPECT_LT(val, 20.0f);  // Upper bound
            EXPECT_GT(val, -50.0f); // Lower bound
        }
    }
}

// Test move semantics
TEST_F(TestKaldiFbankOp, MoveSemantics) {
    OpKaldiFbank fbank1;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    fbank1.init(config);
    
    // Move constructor
    OpKaldiFbank fbank2 = std::move(fbank1);
    EXPECT_EQ(fbank2.feature_dim(), 80);
    
    // Move assignment
    OpKaldiFbank fbank3;
    fbank3 = std::move(fbank2);
    EXPECT_EQ(fbank3.feature_dim(), 80);
}
