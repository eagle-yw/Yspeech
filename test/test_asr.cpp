#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cmath>

import yspeech.context;
import yspeech.types;
import yspeech.speech_processor;
import yspeech.op.asr.base;
import yspeech.op.asr.paraformer;
import yspeech.op.asr.whisper;
import yspeech.op.asr.sensevoice;
import yspeech.op.feature.kaldi_fbank;

using namespace yspeech;

class TestAsrBase : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 30);
    }

    Context ctx_;
};

// Test ASR Base
TEST_F(TestAsrBase, AsrResultStructure) {
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

TEST_F(TestAsrBase, WordInfoStructure) {
    WordInfo word;
    word.word = "Hello";
    word.start_time_ms = 100.0f;
    word.end_time_ms = 300.0f;
    word.confidence = 0.92f;

    EXPECT_EQ(word.word, "Hello");
    EXPECT_FLOAT_EQ(word.start_time_ms, 100.0f);
}

// Test Feature Extraction
class TestFeatureExtract : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 30);
    }

    Context ctx_;
};

TEST_F(TestFeatureExtract, BasicInit) {
    OpKaldiFbank extractor;

    nlohmann::json config;
    config["num_bins"] = 80;
    config["sample_rate"] = 16000;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";

    EXPECT_NO_THROW(extractor.init(config));
}

TEST_F(TestFeatureExtract, ProcessAudio) {
    OpKaldiFbank extractor;

    nlohmann::json config;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";
    config["num_bins"] = 80;
    config["sample_rate"] = 16000;

    extractor.init(config);

    std::vector<float> audio_data(16000);
    for (size_t i = 0; i < audio_data.size(); ++i) {
        audio_data[i] = std::sin(2.0f * M_PI * 440.0f * i / 16000.0f) * 0.5f;
    }

    for (float sample : audio_data) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    extractor.process(ctx_);

    EXPECT_TRUE(ctx_.contains("fbank_num_frames"));
    int num_frames = ctx_.get<int>("fbank_num_frames");
    EXPECT_GT(num_frames, 0);
}

TEST_F(TestFeatureExtract, KaldiFbankFeatureDim) {
    OpKaldiFbank extractor;

    nlohmann::json config;
    config["num_bins"] = 80;
    config["sample_rate"] = 16000;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "fbank";

    extractor.init(config);

    std::vector<float> audio(16000 * 2);
    for (size_t i = 0; i < audio.size(); ++i) {
        audio[i] = std::sin(2.0f * M_PI * 440.0f * i / 16000.0f) * 0.5f;
    }

    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    extractor.process(ctx_);

    auto features = ctx_.get<std::vector<std::vector<float>>>("fbank_features");
    EXPECT_GT(features.size(), 0);
    EXPECT_EQ(features[0].size(), 80);
}

// Test ParaFormer ASR
class TestAsrParaformer : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 30);
    }

    bool model_exists() const {
        std::ifstream model("test_data/paraformer.onnx");
        return model.good();
    }

    Context ctx_;
};

TEST_F(TestAsrParaformer, BasicInit) {
    if (!model_exists()) {
        GTEST_SKIP() << "ParaFormer model files not found";
    }

    OpAsrParaformer asr;

    nlohmann::json config;
    config["model_path"] = "test_data/paraformer.onnx";
    config["tokens_path"] = "test_data/paraformer_tokens.txt";
    config["language"] = "zh";

    EXPECT_NO_THROW(asr.init(config));
}

TEST_F(TestAsrParaformer, ConfigParameters) {
    OpAsrParaformer asr;

    nlohmann::json config;
    config["model_path"] = "test_data/paraformer.onnx";
    config["tokens_path"] = "test_data/paraformer_tokens.txt";
    config["language"] = "zh";
    config["sample_rate"] = 16000;
    config["num_threads"] = 4;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "asr";

    EXPECT_NO_THROW(asr.init(config));
}

// Test Whisper ASR
class TestAsrWhisper : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 30);
    }

    bool model_exists() const {
        std::ifstream encoder("test_data/whisper_encoder.onnx");
        std::ifstream decoder("test_data/whisper_decoder.onnx");
        return encoder.good() && decoder.good();
    }

    Context ctx_;
};

TEST_F(TestAsrWhisper, BasicInit) {
    if (!model_exists()) {
        GTEST_SKIP() << "Whisper model files not found";
    }

    OpAsrWhisper asr;

    nlohmann::json config;
    config["encoder_path"] = "test_data/whisper_encoder.onnx";
    config["decoder_path"] = "test_data/whisper_decoder.onnx";
    config["tokens_path"] = "test_data/whisper_tokens.txt";
    config["language"] = "zh";
    config["task"] = "transcribe";

    EXPECT_NO_THROW(asr.init(config));
}

TEST_F(TestAsrWhisper, ConfigParameters) {
    OpAsrWhisper asr;

    nlohmann::json config;
    config["encoder_path"] = "test_data/whisper_encoder.onnx";
    config["decoder_path"] = "test_data/whisper_decoder.onnx";
    config["tokens_path"] = "test_data/whisper_tokens.txt";
    config["language"] = "en";
    config["task"] = "transcribe";
    config["detect_language"] = true;
    config["sample_rate"] = 16000;

    EXPECT_NO_THROW(asr.init(config));
}

// Test SenseVoice ASR
class TestAsrSenseVoice : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 30);
    }

    bool model_exists() const {
        std::ifstream file("test_data/sensevoice.onnx");
        std::ifstream tokens("test_data/sensevoice_tokens.txt");
        return file.good() && tokens.good();
    }

    Context ctx_;
};

TEST_F(TestAsrSenseVoice, BasicInit) {
    if (!model_exists()) {
        GTEST_SKIP() << "SenseVoice model files not found";
    }

    OpAsrSenseVoice asr;

    nlohmann::json config;
    config["model_path"] = "test_data/sensevoice.onnx";
    config["tokens_path"] = "test_data/sensevoice_tokens.txt";
    config["language"] = "zh";

    EXPECT_NO_THROW(asr.init(config));
}

TEST_F(TestAsrSenseVoice, EmotionDetection) {
    if (!model_exists()) {
        GTEST_SKIP() << "SenseVoice model files not found";
    }

    OpAsrSenseVoice asr;

    nlohmann::json config;
    config["model_path"] = "test_data/sensevoice.onnx";
    config["tokens_path"] = "test_data/sensevoice_tokens.txt";
    config["detect_emotion"] = true;
    config["detect_itn"] = true;

    EXPECT_NO_THROW(asr.init(config));
}

// Test ASR with audio processing
TEST_F(TestAsrParaformer, ProcessEmptyBuffer) {
    OpAsrParaformer asr;

    nlohmann::json config;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "asr";

    asr.init(config);

    EXPECT_NO_THROW(asr.process(ctx_));
    EXPECT_FALSE(ctx_.contains("asr_text"));
}

TEST_F(TestAsrWhisper, ProcessAudio) {
    if (!model_exists()) {
        GTEST_SKIP() << "Whisper model files not found";
    }

    OpAsrWhisper asr;

    nlohmann::json config;
    config["encoder_path"] = "test_data/whisper_encoder.onnx";
    config["decoder_path"] = "test_data/whisper_decoder.onnx";
    config["tokens_path"] = "test_data/whisper_tokens.txt";
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "asr";

    asr.init(config);

    // Generate test audio
    std::vector<float> audio_data(16000 * 5);  // 5 seconds
    for (size_t i = 0; i < audio_data.size(); ++i) {
        audio_data[i] = std::sin(2.0f * M_PI * 440.0f * i / 16000.0f) * 0.3f;
    }

    for (float sample : audio_data) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    asr.process(ctx_);

    // Should have some output (even if placeholder)
    EXPECT_TRUE(ctx_.contains("asr_text"));
}
