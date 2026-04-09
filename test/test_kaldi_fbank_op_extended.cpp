#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <cmath>
#include <fstream>

import std;
import yspeech.domain.feature.base;
import yspeech.domain.feature.kaldi_fbank;

using namespace yspeech;

namespace {

std::vector<float> generate_audio(float duration_sec, float dc_offset = 0.0f) {
    std::vector<float> audio;
    const int sample_rate = 16000;
    const int num_samples = static_cast<int>(duration_sec * static_cast<float>(sample_rate));
    audio.reserve(static_cast<std::size_t>(num_samples));
    for (int i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        audio.push_back(0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * t) + dc_offset);
    }
    return audio;
}

auto create_fbank(const nlohmann::json& extra = {}) -> std::unique_ptr<FeatureCoreIface> {
    auto fbank = FeatureCoreFactory::get_instance().create_core("KaldiFbank");
    nlohmann::json config = {
        {"sample_rate", 16000},
        {"num_bins", 80}
    };
    for (const auto& [key, value] : extra.items()) {
        config[key] = value;
    }
    fbank->init(config);
    return fbank;
}

}

TEST(TestKaldiFbankExtended, WindowTypes) {
    for (const auto* window_type : {"povey", "hamming", "hanning"}) {
        auto fbank = create_fbank({{"window_type", window_type}});
        auto result = fbank->process_samples(generate_audio(1.0f), true);
        EXPECT_NO_THROW((void)result);
        ASSERT_TRUE(result.has_value());
        EXPECT_FALSE(result->features.empty());
    }
}

TEST(TestKaldiFbankExtended, PreemphasisAndDcOffset) {
    auto fbank = create_fbank({
        {"preemph_coeff", 0.97f},
        {"remove_dc_offset", true}
    });
    auto result = fbank->process_samples(generate_audio(2.0f, 0.5f), true);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->features.empty());
}

TEST(TestKaldiFbankExtended, EmptyAndShortAudio) {
    auto fbank = create_fbank();
    auto empty = fbank->process_samples({}, false);
    EXPECT_FALSE(empty.has_value());
    EXPECT_NO_THROW((void)fbank->process_samples(generate_audio(0.02f), true));
}

TEST(TestKaldiFbankExtended, FeatureValuesReasonable) {
    auto fbank = create_fbank();
    auto result = fbank->process_samples(generate_audio(2.0f), true);
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->features.empty());
    for (const auto& frame : result->features) {
        for (float value : frame) {
            EXPECT_TRUE(std::isfinite(value));
            EXPECT_LT(value, 20.0f);
            EXPECT_GT(value, -50.0f);
        }
    }
}

TEST(TestKaldiFbankExtended, FactoryCreatesFreshInstances) {
    auto fbank1 = create_fbank();
    auto fbank2 = create_fbank();
    auto result1 = fbank1->process_samples(generate_audio(0.2f), true);
    auto result2 = fbank2->process_samples(generate_audio(0.2f), true);
    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result1->num_bins, result2->num_bins);
}
