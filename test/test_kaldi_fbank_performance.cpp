#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <cmath>
#include <chrono>
#include <fstream>

import yspeech.context;
import yspeech.op.feature.kaldi_fbank;

using namespace yspeech;

class TestKaldiFbankPerformance : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 60);
    }

    std::vector<float> generate_audio(float duration_sec) {
        std::vector<float> audio;
        int sample_rate = 16000;
        int num_samples = static_cast<int>(duration_sec * sample_rate);
        for (int i = 0; i < num_samples; ++i) {
            float t = static_cast<float>(i) / sample_rate;
            audio.push_back(0.5f * std::sin(2.0f * M_PI * 440.0f * t));
        }
        return audio;
    }

    Context ctx_;
};

// Benchmark feature extraction
TEST_F(TestKaldiFbankPerformance, BenchmarkFeatureExtraction) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    fbank.init(config);

    std::vector<float> durations = {1.0f, 5.0f, 10.0f, 30.0f, 60.0f};
    
    std::cout << "\n=== Feature Extraction Performance ===" << std::endl;
    std::cout << "Duration(s) | Frames | Time(ms) | RTF | Real-time?" << std::endl;
    std::cout << "------------|--------|----------|-----|------------" << std::endl;

    for (float duration : durations) {
        auto audio = generate_audio(duration);
        
        // Warm up
        for (float sample : audio) {
            ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
        }
        fbank.process(ctx_);
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 60);

        // Benchmark
        for (float sample : audio) {
            ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        fbank.process(ctx_);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        int frames = ctx_.get<int>("fbank_num_frames");
        double rtf = elapsed_ms / (duration * 1000.0);
        bool realtime = rtf < 1.0;
        
        std::cout << std::fixed << std::setprecision(1);
        std::cout << std::setw(11) << duration << " | ";
        std::cout << std::setw(6) << frames << " | ";
        std::cout << std::setw(8) << elapsed_ms << " | ";
        std::cout << std::setw(3) << rtf << " | ";
        std::cout << (realtime ? "YES ✓" : "NO ✗") << std::endl;
        
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 60);
    }
    std::cout << std::endl;
}

// Benchmark different num_bins
TEST_F(TestKaldiFbankPerformance, BenchmarkDifferentBins) {
    std::vector<int> bins_list = {20, 40, 64, 80, 128};
    auto audio = generate_audio(10.0f);
    
    std::cout << "\n=== Different Num Bins Performance ===" << std::endl;
    std::cout << "Num Bins | Time(ms) | Speedup" << std::endl;
    std::cout << "---------|----------|--------" << std::endl;
    
    double baseline_time = 0;
    
    for (size_t i = 0; i < bins_list.size(); ++i) {
        OpKaldiFbank fbank;
        nlohmann::json config;
        config["samp_freq"] = 16000.0f;
        config["num_bins"] = bins_list[i];
        config["input_buffer_key"] = "audio_planar";
        config["output_key"] = "fbank";
        fbank.init(config);
        
        for (float sample : audio) {
            ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        fbank.process(ctx_);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        
        if (i == 0) baseline_time = elapsed_ms;
        double speedup = baseline_time / elapsed_ms;
        
        std::cout << std::setw(8) << bins_list[i] << " | ";
        std::cout << std::setw(8) << elapsed_ms << " | ";
        std::cout << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;
        
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 60);
    }
    std::cout << std::endl;
}

// Benchmark memory usage
TEST_F(TestKaldiFbankPerformance, BenchmarkMemoryUsage) {
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    fbank.init(config);
    
    std::vector<float> durations = {1.0f, 10.0f, 60.0f};
    
    std::cout << "\n=== Memory Usage Analysis ===" << std::endl;
    std::cout << "Duration(s) | Audio(MB) | Features(MB) | Total(MB)" << std::endl;
    std::cout << "------------|-----------|--------------|----------" << std::endl;
    
    for (float duration : durations) {
        auto audio = generate_audio(duration);
        
        // Audio size
        size_t audio_bytes = audio.size() * sizeof(float);
        double audio_mb = audio_bytes / (1024.0 * 1024.0);
        
        for (float sample : audio) {
            ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
        }
        
        fbank.process(ctx_);
        
        auto features = ctx_.get<std::vector<std::vector<float>>>("fbank_features");
        size_t feature_bytes = features.size() * features[0].size() * sizeof(float);
        double feature_mb = feature_bytes / (1024.0 * 1024.0);
        
        std::cout << std::fixed << std::setprecision(1);
        std::cout << std::setw(11) << duration << " | ";
        std::cout << std::setw(9) << audio_mb << " | ";
        std::cout << std::setw(12) << feature_mb << " | ";
        std::cout << std::setw(10) << (audio_mb + feature_mb) << std::endl;
        
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 60);
    }
    std::cout << std::endl;
}

// Benchmark with real audio file
TEST_F(TestKaldiFbankPerformance, BenchmarkRealAudio) {
    std::ifstream test_file("test_data/test_zh.wav");
    if (!test_file.good()) {
        GTEST_SKIP() << "Real audio file not found";
    }
    
    OpKaldiFbank fbank;
    nlohmann::json config;
    config["samp_freq"] = 16000.0f;
    config["num_bins"] = 80;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    fbank.init(config);
    
    // Load audio
    std::ifstream file("test_data/test_zh.wav", std::ios::binary);
    file.seekg(44);  // Skip header
    std::vector<int16_t> pcm_data;
    int16_t sample;
    while (file.read(reinterpret_cast<char*>(&sample), sizeof(sample))) {
        pcm_data.push_back(sample);
    }
    
    // Convert to float
    for (int16_t s : pcm_data) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(static_cast<float>(s) / 32768.0f);
    }
    
    float duration = pcm_data.size() / 16000.0f;
    
    auto start = std::chrono::high_resolution_clock::now();
    fbank.process(ctx_);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
    int frames = ctx_.get<int>("fbank_num_frames");
    double rtf = elapsed_ms / (duration * 1000.0);
    
    std::cout << "\n=== Real Audio Performance ===" << std::endl;
    std::cout << "Duration: " << duration << "s" << std::endl;
    std::cout << "Frames: " << frames << std::endl;
    std::cout << "Processing time: " << elapsed_ms << "ms" << std::endl;
    std::cout << "RTF: " << rtf << std::endl;
    std::cout << "Real-time capable: " << (rtf < 1.0 ? "YES ✓" : "NO ✗") << std::endl;
    std::cout << std::endl;
}
