#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cmath>

import std;
import yspeech.types;
import yspeech.domain.asr.base;
import yspeech.domain.asr.paraformer;
import yspeech.domain.asr.whisper;
import yspeech.domain.asr.sensevoice;
import yspeech.domain.feature.base;
import yspeech.domain.feature.kaldi_fbank;

using namespace yspeech;

namespace {

std::vector<float> make_sine(std::size_t samples, float freq = 440.0f) {
    std::vector<float> audio(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        audio[i] = std::sin(2.0f * static_cast<float>(M_PI) * freq *
                            static_cast<float>(i) / 16000.0f) * 0.5f;
    }
    return audio;
}

bool paraformer_model_exists() {
    std::ifstream model("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx");
    std::ifstream tokens("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt");
    return model.good() && tokens.good();
}

bool whisper_model_exists() {
    std::ifstream encoder("test_data/whisper_encoder.onnx");
    std::ifstream decoder("test_data/whisper_decoder.onnx");
    return encoder.good() && decoder.good();
}

bool sensevoice_model_exists() {
    std::ifstream model("model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/model.int8.onnx");
    std::ifstream tokens("model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/tokens.txt");
    return model.good() && tokens.good();
}

auto create_fbank_core(const nlohmann::json& extra = {}) -> std::unique_ptr<FeatureCoreIface> {
    auto extractor = FeatureCoreFactory::get_instance().create_core("KaldiFbank");
    nlohmann::json config = {
        {"sample_rate", 16000},
        {"num_bins", 80}
    };
    for (const auto& [key, value] : extra.items()) {
        config[key] = value;
    }
    extractor->init(config);
    return extractor;
}

}

TEST(TestAsrBase, AsrResultStructure) {
    AsrResult result;
    result.text = "Hello World";
    result.confidence = 0.95f;
    result.language = "en";
    result.start_time_ms = 0.0f;
    result.end_time_ms = 1000.0f;

    EXPECT_EQ(result.text, "Hello World");
    EXPECT_FLOAT_EQ(result.confidence, 0.95f);
    EXPECT_EQ(result.language, "en");
}

TEST(TestAsrBase, WordInfoStructure) {
    WordInfo word;
    word.word = "Hello";
    word.start_time_ms = 100.0f;
    word.end_time_ms = 300.0f;
    word.confidence = 0.92f;

    EXPECT_EQ(word.word, "Hello");
    EXPECT_FLOAT_EQ(word.start_time_ms, 100.0f);
}

TEST(TestFeatureExtract, BasicInit) {
    auto extractor = create_fbank_core();
    nlohmann::json config = {
        {"num_bins", 80},
        {"sample_rate", 16000}
    };
    EXPECT_NO_THROW(extractor->deinit());
}

TEST(TestFeatureExtract, ProcessAudioStream) {
    auto extractor = create_fbank_core();
    auto result = extractor->process_samples(make_sine(16000), true);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->num_frames, 0);
    extractor->deinit();
}

TEST(TestFeatureExtract, KaldiFbankFeatureDim) {
    auto extractor = create_fbank_core();
    auto result = extractor->process_samples(make_sine(32000), true);
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->features.empty());
    EXPECT_EQ(result->features.front().size(), 80);
    extractor->deinit();
}

TEST(TestAsrParaformer, BasicInit) {
    if (!paraformer_model_exists()) {
        GTEST_SKIP() << "ParaFormer model files not found";
    }

    auto asr = AsrCoreFactory::get_instance().create_core("AsrParaformer");
    nlohmann::json config = {
        {"model_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx"},
        {"tokens_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt"},
        {"language", "zh"}
    };
    EXPECT_NO_THROW(asr->init(config));
    asr->deinit();
}

TEST(TestAsrParaformer, EmptyFeaturesNeedMoreInput) {
    if (!paraformer_model_exists()) {
        GTEST_SKIP() << "ParaFormer model files not found";
    }

    auto asr = AsrCoreFactory::get_instance().create_core("AsrParaformer");
    nlohmann::json config = {
        {"model_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx"},
        {"tokens_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt"},
        {"language", "zh"}
    };
    asr->init(config);
    auto result = asr->infer(FeatureSequenceView{});
    EXPECT_TRUE(result.text.empty());
    asr->deinit();
}

TEST(TestAsrWhisper, BasicInit) {
    if (!whisper_model_exists()) {
        GTEST_SKIP() << "Whisper model files not found";
    }

    auto asr = AsrCoreFactory::get_instance().create_core("AsrWhisper");
    nlohmann::json config = {
        {"encoder_path", "test_data/whisper_encoder.onnx"},
        {"decoder_path", "test_data/whisper_decoder.onnx"},
        {"tokens_path", "test_data/whisper_tokens.txt"},
        {"language", "zh"},
        {"task", "transcribe"}
    };
    EXPECT_NO_THROW(asr->init(config));
    asr->deinit();
}

TEST(TestAsrSenseVoice, BasicInit) {
    if (!sensevoice_model_exists()) {
        GTEST_SKIP() << "SenseVoice model files not found";
    }

    auto asr = AsrCoreFactory::get_instance().create_core("AsrSenseVoice");
    nlohmann::json config = {
        {"model_path", "model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/model.int8.onnx"},
        {"tokens_path", "model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/tokens.txt"},
        {"language", "zh"}
    };
    EXPECT_NO_THROW(asr->init(config));
    asr->deinit();
}
