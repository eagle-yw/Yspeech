#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <cmath>

import std;
import yspeech.domain.feature.base;
import yspeech.domain.feature.kaldi_fbank;

using namespace yspeech;

namespace {

std::vector<float> generate_audio(float duration_sec, float freq = 440.0f) {
    std::vector<float> audio;
    const int sample_rate = 16000;
    const int num_samples = static_cast<int>(duration_sec * static_cast<float>(sample_rate));
    audio.reserve(static_cast<std::size_t>(num_samples));
    for (int i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        audio.push_back(0.5f * std::sin(2.0f * static_cast<float>(M_PI) * freq * t));
    }
    return audio;
}

auto create_fbank(const nlohmann::json& extra = {}) -> std::unique_ptr<FeatureCoreIface> {
    auto fbank = FeatureCoreFactory::get_instance().create_core("KaldiFbank");
    nlohmann::json config = {
        {"sample_rate", 16000},
        {"num_bins", 80},
        {"frame_length_ms", 25.0f},
        {"frame_shift_ms", 10.0f}
    };
    for (const auto& [key, value] : extra.items()) {
        config[key] = value;
    }
    fbank->init(config);
    return fbank;
}

}

TEST(TestKaldiFbankOp, BasicInit) {
    auto fbank = create_fbank();
    auto result = fbank->process_samples(generate_audio(0.5f), true);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->num_bins, 80);
}

TEST(TestKaldiFbankOp, ExtractFeatures) {
    auto fbank = create_fbank();
    auto result = fbank->process_samples(generate_audio(3.0f), true);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->num_frames, 0);
    EXPECT_EQ(result->num_bins, 80);
    EXPECT_FALSE(result->features.empty());
}

TEST(TestKaldiFbankOp, DifferentConfigs) {
    auto fbank40 = create_fbank({{"num_bins", 40}});
    auto result40 = fbank40->process_samples(generate_audio(0.5f), true);
    ASSERT_TRUE(result40.has_value());
    EXPECT_EQ(result40->num_bins, 40);

    auto fast_shift = create_fbank({
        {"frame_length_ms", 20.0f},
        {"frame_shift_ms", 5.0f}
    });
    auto fast_result = fast_shift->process_samples(generate_audio(0.5f), true);
    ASSERT_TRUE(fast_result.has_value());
    EXPECT_FALSE(fast_result->features.empty());
}

TEST(TestKaldiFbankOp, DifferentFrequencies) {
    for (float freq : {100.0f, 440.0f, 1000.0f, 4000.0f}) {
        auto fbank = create_fbank();
        auto result = fbank->process_samples(generate_audio(0.5f, freq), true);
        EXPECT_TRUE(result.has_value()) << freq;
        EXPECT_FALSE(result->features.empty()) << freq;
    }
}

TEST(TestKaldiFbankOp, LowFrameRateReduction) {
    auto fbank = create_fbank({
        {"lfr_window_size", 7},
        {"lfr_window_shift", 6}
    });
    auto result = fbank->process_samples(generate_audio(1.0f), true);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->num_bins, 560);
}

TEST(TestKaldiFbankOp, Deinit) {
    auto fbank = create_fbank();
    EXPECT_NO_THROW(fbank->deinit());
}

TEST(TestKaldiFbankOp, AccumulationSeparatesDeltaAndAccumulatedViews) {
    auto fbank = create_fbank({
        {"enable_accumulation", true},
        {"min_accumulated_frames", 1},
        {"max_accumulated_frames", 100}
    });

    auto first = fbank->process_samples(generate_audio(0.3f), false);
    ASSERT_TRUE(first.has_value());
    ASSERT_FALSE(first->delta_features.empty());
    ASSERT_FALSE(first->features.empty());
    EXPECT_EQ(first->delta_num_frames, static_cast<int>(first->delta_features.size()));
    EXPECT_EQ(first->accumulated_num_frames, static_cast<int>(first->features.size()));
    EXPECT_EQ(first->delta_num_frames, first->accumulated_num_frames);

    auto second = fbank->process_samples(generate_audio(0.3f), false);
    ASSERT_TRUE(second.has_value());
    ASSERT_FALSE(second->delta_features.empty());
    ASSERT_FALSE(second->features.empty());
    EXPECT_EQ(second->delta_num_frames, static_cast<int>(second->delta_features.size()));
    EXPECT_EQ(second->accumulated_num_frames, static_cast<int>(second->features.size()));
    EXPECT_GT(second->accumulated_num_frames, second->delta_num_frames);
    EXPECT_EQ(second->num_frames, second->accumulated_num_frames);
}
