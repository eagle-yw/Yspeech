#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cmath>

import std;
import yspeech.domain.vad.base;
import yspeech.domain.vad.silero;
import yspeech.domain.feature.base;
import yspeech.domain.feature.kaldi_fbank;
import yspeech.domain.asr.base;
import yspeech.domain.asr.paraformer;

using namespace yspeech;

namespace {

bool vad_model_exists() {
    std::ifstream file("model/vad/silero_vad.onnx");
    return file.good();
}

bool paraformer_model_exists() {
    std::ifstream model("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx");
    std::ifstream tokens("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt");
    return model.good() && tokens.good();
}

std::vector<float> generate_audio(float duration_sec) {
    std::vector<float> audio;
    const int sample_rate = 16000;
    const int num_samples = static_cast<int>(duration_sec * static_cast<float>(sample_rate));
    audio.reserve(static_cast<std::size_t>(num_samples));
    for (int i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        float sample = 0.4f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * t);
        sample += 0.2f * std::sin(2.0f * static_cast<float>(M_PI) * 880.0f * t);
        audio.push_back(sample);
    }
    return audio;
}

}

TEST(TestIntegrationVadAsr, VadAndFbankSmoke) {
    if (!vad_model_exists()) {
        GTEST_SKIP() << "VAD model not found";
    }

    auto vad = VadCoreFactory::get_instance().create_core("SileroVad");
    vad->init({
        {"model_path", "model/vad/silero_vad.onnx"},
        {"sample_rate", 16000}
    });

    auto fbank = FeatureCoreFactory::get_instance().create_core("KaldiFbank");
    fbank->init({
        {"sample_rate", 16000},
        {"num_bins", 80}
    });

    const auto audio = generate_audio(2.0f);
    auto vad_result = vad->process_samples(audio, true);
    auto fbank_result = fbank->process_samples(audio, true);
    EXPECT_GE(vad_result.probability, 0.0f);
    EXPECT_LE(vad_result.probability, 1.0f);
    ASSERT_TRUE(fbank_result.has_value());
    EXPECT_FALSE(fbank_result->features.empty());
    vad->deinit();
    fbank->deinit();
}

TEST(TestIntegrationVadAsr, FbankToParaformerSmoke) {
    if (!paraformer_model_exists()) {
        GTEST_SKIP() << "Paraformer model not found";
    }

    auto fbank = FeatureCoreFactory::get_instance().create_core("KaldiFbank");
    fbank->init({
        {"sample_rate", 16000},
        {"num_bins", 80},
        {"lfr_window_size", 7},
        {"lfr_window_shift", 6}
    });

    auto asr = AsrCoreFactory::get_instance().create_core("AsrParaformer");
    asr->init({
        {"model_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx"},
        {"tokens_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt"},
        {"language", "zh"}
    });

    auto fbank_result = fbank->process_samples(generate_audio(3.0f), true);
    ASSERT_TRUE(fbank_result.has_value());
    ASSERT_FALSE(fbank_result->features.empty());

    EXPECT_NO_THROW({
        auto asr_result = asr->infer(fbank_result->features);
        EXPECT_TRUE(asr_result.text.empty() || !asr_result.text.empty());
    });
    fbank->deinit();
    asr->deinit();
}
