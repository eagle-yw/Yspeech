#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cmath>

import std;
import yspeech.types;
import yspeech.domain.vad.base;
import yspeech.domain.vad.silero;

using namespace yspeech;

namespace {

bool model_exists() {
    std::ifstream file("model/vad/silero_vad.onnx");
    return file.good();
}

std::vector<float> make_audio(std::size_t samples, float scale = 0.1f) {
    std::vector<float> audio(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        audio[i] = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * static_cast<float>(i) / 16000.0f) * scale;
    }
    return audio;
}

auto create_vad(float threshold = 0.5f) -> std::unique_ptr<VadCoreIface> {
    auto vad = VadCoreFactory::get_instance().create_core("SileroVad");
    nlohmann::json config = {
        {"model_path", "model/vad/silero_vad.onnx"},
        {"threshold", threshold},
        {"sample_rate", 16000}
    };
    vad->init(config);
    return vad;
}

}

TEST(TestSileroVad, BasicInit) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }
    EXPECT_NO_THROW(create_vad());
}

TEST(TestSileroVad, ProcessEmptyStore) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }

    auto vad = create_vad();
    auto result = vad->process_samples({}, false);
    EXPECT_FLOAT_EQ(result.probability, 0.0f);
    EXPECT_FALSE(result.is_speech);
    EXPECT_TRUE(result.finished_segments.empty());
}

TEST(TestSileroVad, ProcessWithAudioData) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }

    auto vad = create_vad(0.5f);
    auto result = vad->process_samples(make_audio(5120), true);
    EXPECT_GE(result.probability, 0.0f);
    EXPECT_LE(result.probability, 1.0f);
}

TEST(TestSileroVad, StateManagement) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }

    auto vad = create_vad();
    EXPECT_FLOAT_EQ(vad->current_probability(), 0.0f);
}

TEST(TestSileroVad, Deinit) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }

    auto vad = create_vad();
    EXPECT_NO_THROW(vad->deinit());
}

TEST(TestSileroVad, RecordsCorePhaseTimings) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }

    auto vad = VadCoreFactory::get_instance().create_core("SileroVad");
    yspeech::ProcessingStats stats;
    nlohmann::json config = {
        {"__core_id", "vad"},
        {"model_path", "model/vad/silero_vad.onnx"},
        {"threshold", 0.5f},
        {"sample_rate", 16000}
    };
    vad->init(config);
    vad->bind_stats(&stats);

    (void)vad->process_samples(make_audio(5120), true);

    EXPECT_TRUE(stats.core_timings.contains("vad"));
    EXPECT_TRUE(stats.core_phase_timings.contains("vad:pack"));
    EXPECT_TRUE(stats.core_phase_timings.contains("vad:run"));
    EXPECT_TRUE(stats.core_phase_timings.contains("vad:decode"));
    EXPECT_TRUE(stats.core_phase_timings.contains("vad:state"));

    vad->deinit();
}
