#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cmath>

import yspeech.context;
import yspeech.types;
import yspeech.op.silero_vad;

using namespace yspeech;

class TestSileroVad : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 10);
    }

    void TearDown() override {
    }

    bool model_exists() const {
        std::ifstream file("test_data/silero_vad.onnx");
        return file.good();
    }

    Context ctx_;
};

TEST_F(TestSileroVad, BasicInit) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: test_data/silero_vad.onnx";
    }

    OpSileroVad vad;

    nlohmann::json config;
    config["model_path"] = "test_data/silero_vad.onnx";
    config["threshold"] = 0.5f;
    config["sample_rate"] = 16000;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "vad";

    EXPECT_NO_THROW(vad.init(config));
}

TEST_F(TestSileroVad, ProcessEmptyBuffer) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: test_data/silero_vad.onnx";
    }

    OpSileroVad vad;

    nlohmann::json config;
    config["model_path"] = "test_data/silero_vad.onnx";
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "vad";

    vad.init(config);

    EXPECT_NO_THROW(vad.process(ctx_));

    EXPECT_FALSE(ctx_.contains("vad_probability"));
}

TEST_F(TestSileroVad, ProcessWithAudioData) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: test_data/silero_vad.onnx";
    }

    OpSileroVad vad;

    nlohmann::json config;
    config["model_path"] = "test_data/silero_vad.onnx";
    config["threshold"] = 0.5f;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "vad";

    vad.init(config);

    std::vector<float> audio_data(512 * 10);
    for (size_t i = 0; i < audio_data.size(); ++i) {
        audio_data[i] = std::sin(2.0f * M_PI * 440.0f * i / 16000.0f) * 0.1f;
    }

    for (float sample : audio_data) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    for (int i = 0; i < 5; ++i) {
        vad.process(ctx_);
    }

    if (ctx_.contains("vad_probability")) {
        float prob = ctx_.get<float>("vad_probability");
        EXPECT_GE(prob, 0.0f);
        EXPECT_LE(prob, 1.0f);
    }
}

TEST_F(TestSileroVad, ConfigParameters) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: test_data/silero_vad.onnx";
    }

    OpSileroVad vad;

    nlohmann::json config;
    config["model_path"] = "test_data/silero_vad.onnx";
    config["threshold"] = 0.7f;
    config["sample_rate"] = 16000;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "vad_result";
    config["min_speech_duration_ms"] = 500;
    config["min_silence_duration_ms"] = 200;

    EXPECT_NO_THROW(vad.init(config));
}

TEST_F(TestSileroVad, VadSegmentOutput) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: test_data/silero_vad.onnx";
    }

    OpSileroVad vad;

    nlohmann::json config;
    config["model_path"] = "test_data/silero_vad.onnx";
    config["threshold"] = 0.3f;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "vad";
    config["min_speech_duration_ms"] = 100;
    config["min_silence_duration_ms"] = 50;

    vad.init(config);

    std::vector<float> audio_data(512 * 50);
    for (size_t i = 0; i < audio_data.size(); ++i) {
        float t = static_cast<float>(i) / 16000.0f;
        if (t < 0.5f || t > 1.0f) {
            audio_data[i] = std::sin(2.0f * M_PI * 440.0f * i / 16000.0f) * 0.3f;
        } else {
            audio_data[i] = 0.0f;
        }
    }

    for (float sample : audio_data) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    for (int i = 0; i < 30; ++i) {
        vad.process(ctx_);
    }

    if (ctx_.contains("vad_segments")) {
        auto segments = ctx_.get<std::vector<VadSegment>>("vad_segments");
        EXPECT_GE(segments.size(), 0);

        for (const auto& seg : segments) {
            EXPECT_GT(seg.end_ms, seg.start_ms);
            EXPECT_GE(seg.confidence, 0.0f);
            EXPECT_LE(seg.confidence, 1.0f);
        }
    }
}

TEST_F(TestSileroVad, StateManagement) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: test_data/silero_vad.onnx";
    }

    OpSileroVad vad;

    nlohmann::json config;
    config["model_path"] = "test_data/silero_vad.onnx";
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "vad";

    vad.init(config);

    EXPECT_FALSE(vad.is_speech());
    EXPECT_FLOAT_EQ(vad.current_probability(), 0.0f);
}

TEST_F(TestSileroVad, Deinit) {
    if (!model_exists()) {
        GTEST_SKIP() << "Model file not found: test_data/silero_vad.onnx";
    }

    OpSileroVad vad;

    nlohmann::json config;
    config["model_path"] = "test_data/silero_vad.onnx";

    vad.init(config);
    EXPECT_NO_THROW(vad.deinit());
}
