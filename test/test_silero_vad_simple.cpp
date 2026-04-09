#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <fstream>

import yspeech.domain.vad.base;
import yspeech.domain.vad.silero;

using namespace yspeech;

TEST(TestSileroVadSimple, ModelExists) {
    std::ifstream file("model/vad/silero_vad.onnx");
    EXPECT_TRUE(file.good()) << "Model file not found: model/vad/silero_vad.onnx";
}

TEST(TestSileroVadSimple, BasicInit) {
    std::ifstream file("model/vad/silero_vad.onnx");
    if (!file.good()) {
        GTEST_SKIP() << "Model file not found: model/vad/silero_vad.onnx";
    }

    auto vad = VadCoreFactory::get_instance().create_core("SileroVad");
    nlohmann::json config;
    config["model_path"] = "model/vad/silero_vad.onnx";
    config["threshold"] = 0.5f;
    config["sample_rate"] = 16000;

    EXPECT_NO_THROW(vad->init(config));
    vad->deinit();
}
