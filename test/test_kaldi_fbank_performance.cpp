#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <cmath>
#include <chrono>

import std;
import yspeech.domain.feature.base;
import yspeech.domain.feature.kaldi_fbank;

using namespace yspeech;

namespace {

std::vector<float> generate_audio(float duration_sec) {
    std::vector<float> audio;
    const int sample_rate = 16000;
    const int num_samples = static_cast<int>(duration_sec * static_cast<float>(sample_rate));
    audio.reserve(static_cast<std::size_t>(num_samples));
    for (int i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        audio.push_back(0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * t));
    }
    return audio;
}

auto create_fbank() -> std::unique_ptr<FeatureCoreIface> {
    auto fbank = FeatureCoreFactory::get_instance().create_core("KaldiFbank");
    nlohmann::json config = {
        {"sample_rate", 16000},
        {"num_bins", 80}
    };
    fbank->init(config);
    return fbank;
}

}

TEST(TestKaldiFbankPerformance, BenchmarkFeatureExtraction) {
    auto fbank = create_fbank();

    const auto begin = std::chrono::steady_clock::now();
    auto result = fbank->process_samples(generate_audio(10.0f), true);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin).count();

    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->num_frames, 0);
    EXPECT_LT(elapsed_ms, 5000);
}

TEST(TestKaldiFbankPerformance, FlushFinalization) {
    auto fbank = create_fbank();
    auto result = fbank->process_samples(generate_audio(1.0f), true);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->num_frames, 0);
}
