#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <fstream>

import yspeech.context;
import yspeech.op.silero_vad;

using namespace yspeech;

TEST(TestSileroVadSimple, ModelExists) {
    std::ifstream file("test_data/silero_vad.onnx");
    EXPECT_TRUE(file.good()) << "Model file not found: test_data/silero_vad.onnx";
}

TEST(TestSileroVadSimple, BasicInit) {
    std::ifstream file("test_data/silero_vad.onnx");
    if (!file.good()) {
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
