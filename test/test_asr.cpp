#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cmath>

import std;
import yspeech.context;
import yspeech.types;
import yspeech.stream_store;
import yspeech.op;
import yspeech.op.asr.base;
import yspeech.op.asr.paraformer;
import yspeech.op.asr.whisper;
import yspeech.op.asr.sensevoice;
import yspeech.op.feature.kaldi_fbank;

using namespace yspeech;

namespace {

AudioFramePtr make_test_frame(const std::vector<float>& samples, std::uint64_t seq, bool eos = false) {
    static AudioFramePool pool;
    auto frame = pool.acquire(samples.size());
    frame->stream_id = "test";
    frame->seq = seq;
    frame->sample_rate = 16000;
    frame->channels = 1;
    frame->pts_ms = static_cast<std::int64_t>(seq * 10);
    frame->dur_ms = 10;
    frame->eos = eos;
    frame->samples = samples;
    return frame;
}

std::vector<float> make_sine(std::size_t samples, float freq = 440.0f) {
    std::vector<float> audio(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        audio[i] = std::sin(2.0f * static_cast<float>(M_PI) * freq *
                            static_cast<float>(i) / 16000.0f) * 0.5f;
    }
    return audio;
}

void push_audio_frames(StreamStore& store, const std::vector<float>& audio, std::string key = "audio_frames") {
    constexpr std::size_t frame_samples = 160;
    std::uint64_t seq = 0;
    for (std::size_t offset = 0; offset < audio.size(); offset += frame_samples, ++seq) {
        const auto end = std::min(offset + frame_samples, audio.size());
        std::vector<float> frame_samples_buffer(audio.begin() + static_cast<std::ptrdiff_t>(offset),
                                                audio.begin() + static_cast<std::ptrdiff_t>(end));
        const bool eos = end == audio.size();
        store.push_frame(key, make_test_frame(frame_samples_buffer, seq, eos));
    }
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
    OpKaldiFbank extractor;
    nlohmann::json config = {
        {"num_bins", 80},
        {"sample_rate", 16000},
        {"input_frame_key", "audio_frames"},
        {"output_key", "fbank"}
    };
    EXPECT_NO_THROW(extractor.init(config));
}

TEST(TestFeatureExtract, ProcessAudioStream) {
    OpKaldiFbank extractor;
    nlohmann::json config = {
        {"input_frame_key", "audio_frames"},
        {"output_key", "fbank"},
        {"num_bins", 80},
        {"sample_rate", 16000}
    };
    extractor.init(config);

    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 2048);
    push_audio_frames(store, make_sine(16000));

    ASSERT_TRUE(extractor.ready(ctx, store));
    auto result = extractor.process_stream(ctx, store);
    EXPECT_NE(result.status, StreamProcessStatus::NeedMoreInput);
    EXPECT_TRUE(ctx.contains("fbank_num_frames"));
    EXPECT_GT(ctx.get<int>("fbank_num_frames"), 0);

    auto flush_result = extractor.flush(ctx, store);
    EXPECT_TRUE(flush_result.status == StreamProcessStatus::NoOp ||
                flush_result.status == StreamProcessStatus::StreamFinalized);
}

TEST(TestFeatureExtract, KaldiFbankFeatureDim) {
    OpKaldiFbank extractor;
    nlohmann::json config = {
        {"num_bins", 80},
        {"sample_rate", 16000},
        {"input_frame_key", "audio_frames"},
        {"output_key", "fbank"}
    };
    extractor.init(config);

    Context ctx;
    StreamStore store;
    store.init_audio_ring("audio_frames", 4096);
    push_audio_frames(store, make_sine(32000));

    extractor.process_stream(ctx, store);
    auto features = ctx.get<std::vector<std::vector<float>>>("fbank_features");
    ASSERT_FALSE(features.empty());
    EXPECT_EQ(features.front().size(), 80);
}

TEST(TestAsrParaformer, BasicInit) {
    if (!paraformer_model_exists()) {
        GTEST_SKIP() << "ParaFormer model files not found";
    }

    OpAsrParaformer asr;
    nlohmann::json config = {
        {"model_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx"},
        {"tokens_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt"},
        {"language", "zh"}
    };
    EXPECT_NO_THROW(asr.init(config));
}

TEST(TestAsrParaformer, EmptyFeaturesNeedMoreInput) {
    if (!paraformer_model_exists()) {
        GTEST_SKIP() << "ParaFormer model files not found";
    }

    OpAsrParaformer asr;
    nlohmann::json config = {
        {"model_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx"},
        {"tokens_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt"},
        {"feature_input_key", "fbank"},
        {"output_key", "asr"}
    };
    asr.init(config);

    Context ctx;
    StreamStore store;
    auto result = asr.process_stream(ctx, store);
    EXPECT_EQ(result.status, StreamProcessStatus::NoOp);
    EXPECT_FALSE(ctx.contains("asr_text"));
}

TEST(TestAsrWhisper, BasicInit) {
    if (!whisper_model_exists()) {
        GTEST_SKIP() << "Whisper model files not found";
    }

    OpAsrWhisper asr;
    nlohmann::json config = {
        {"encoder_path", "test_data/whisper_encoder.onnx"},
        {"decoder_path", "test_data/whisper_decoder.onnx"},
        {"tokens_path", "test_data/whisper_tokens.txt"},
        {"language", "zh"},
        {"task", "transcribe"}
    };
    EXPECT_NO_THROW(asr.init(config));
}

TEST(TestAsrSenseVoice, BasicInit) {
    if (!sensevoice_model_exists()) {
        GTEST_SKIP() << "SenseVoice model files not found";
    }

    OpAsrSenseVoice asr;
    nlohmann::json config = {
        {"model_path", "model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/model.int8.onnx"},
        {"tokens_path", "model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/tokens.txt"},
        {"language", "zh"}
    };
    EXPECT_NO_THROW(asr.init(config));
}
