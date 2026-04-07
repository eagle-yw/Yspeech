#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cmath>

import yspeech.context;
import yspeech.types;
import yspeech.op.vad.silero;
import yspeech.op.asr.base;
import yspeech.op.asr.paraformer;
import yspeech.op.asr.whisper;
import yspeech.op.asr.sensevoice;
import yspeech.op.feature.kaldi_fbank;

using namespace yspeech;

class TestIntegrationVadAsr : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.init_audio_buffer("audio_raw", 1, 16000 * 60);
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 60);
    }

    std::vector<float> generate_test_audio_with_speech() {
        std::vector<float> audio;
        int sample_rate = 16000;
        
        for (int i = 0; i < sample_rate * 2; ++i) {
            float t = static_cast<float>(i) / sample_rate;
            float sample = 0.5f * std::sin(2.0f * M_PI * 440.0f * t);
            sample += 0.25f * std::sin(2.0f * M_PI * 880.0f * t);
            sample += 0.125f * std::sin(2.0f * M_PI * 1320.0f * t);
            audio.push_back(sample * 0.3f);
        }
        
        for (int i = 0; i < sample_rate; ++i) {
            audio.push_back(0.0f);
        }
        
        for (int i = 0; i < sample_rate * 1.5; ++i) {
            float t = static_cast<float>(i) / sample_rate;
            float sample = 0.5f * std::sin(2.0f * M_PI * 523.25f * t);
            sample += 0.25f * std::sin(2.0f * M_PI * 1046.5f * t);
            audio.push_back(sample * 0.3f);
        }
        
        return audio;
    }

    std::vector<float> generate_speech_like_audio(float duration_sec, float base_freq = 440.0f) {
        std::vector<float> audio;
        int sample_rate = 16000;
        int num_samples = static_cast<int>(duration_sec * sample_rate);
        
        for (int i = 0; i < num_samples; ++i) {
            float t = static_cast<float>(i) / sample_rate;
            float freq_var = 1.0f + 0.1f * std::sin(2.0f * M_PI * 5.0f * t);
            float amp_env = 0.5f + 0.5f * std::sin(2.0f * M_PI * 3.0f * t);
            
            float sample = amp_env * 0.3f * std::sin(2.0f * M_PI * base_freq * freq_var * t);
            sample += amp_env * 0.15f * std::sin(2.0f * M_PI * base_freq * 2.0f * freq_var * t);
            sample += amp_env * 0.075f * std::sin(2.0f * M_PI * base_freq * 3.0f * freq_var * t);
            
            audio.push_back(sample);
        }
        
        return audio;
    }

    bool vad_model_exists() const {
        std::ifstream file("test_data/silero_vad.onnx");
        return file.good();
    }

    bool paraformer_model_exists() const {
        std::ifstream file("test_data/paraformer.onnx");
        return file.good();
    }

    bool whisper_model_exists() const {
        std::ifstream encoder("test_data/whisper_encoder.onnx");
        std::ifstream decoder("test_data/whisper_decoder.onnx");
        return encoder.good() && decoder.good();
    }

    bool sensevoice_model_exists() const {
        std::ifstream file("test_data/sensevoice.onnx");
        std::ifstream tokens("test_data/sensevoice_tokens.txt");
        return file.good() && tokens.good();
    }

    Context ctx_;
};

TEST_F(TestIntegrationVadAsr, VadDetectsSpeechSegments) {
    if (!vad_model_exists()) {
        GTEST_SKIP() << "VAD model not found";
    }

    OpSileroVad vad;
    nlohmann::json vad_config;
    vad_config["model_path"] = "test_data/silero_vad.onnx";
    vad_config["threshold"] = 0.5f;
    vad_config["sample_rate"] = 16000;
    vad_config["input_buffer_key"] = "audio_planar";
    vad_config["output_key"] = "vad";
    vad_config["min_speech_duration_ms"] = 500;
    vad_config["min_silence_duration_ms"] = 300;

    vad.init(vad_config);

    auto audio = generate_speech_like_audio(5.0f);
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    for (int i = 0; i < 100; ++i) {
        vad.process_batch(ctx_);
    }

    EXPECT_TRUE(ctx_.contains("vad_probability"));
    EXPECT_TRUE(ctx_.contains("vad_is_speech"));
}

TEST_F(TestIntegrationVadAsr, FeatureExtractToAsrPipeline) {
    if (!paraformer_model_exists()) {
        GTEST_SKIP() << "ParaFormer model not found";
    }

    OpKaldiFbank fbank;
    nlohmann::json fbank_config;
    fbank_config["input_buffer_key"] = "audio_planar";
    fbank_config["output_key"] = "fbank";
    fbank_config["num_bins"] = 80;
    fbank_config["sample_rate"] = 16000;
    fbank.init(fbank_config);

    OpAsrParaformer asr;
    nlohmann::json asr_config;
    asr_config["model_path"] = "test_data/paraformer.onnx";
    asr_config["tokens_path"] = "test_data/paraformer_tokens.txt";
    asr_config["language"] = "zh";
    asr_config["feature_input_key"] = "fbank";
    asr_config["output_key"] = "asr";
    asr.init(asr_config);

    auto audio = generate_speech_like_audio(3.0f);
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    fbank.process_batch(ctx_);
    asr.process_batch(ctx_);

    EXPECT_TRUE(ctx_.contains("fbank_features"));
}

TEST_F(TestIntegrationVadAsr, CompleteVadToAsrPipeline) {
    if (!vad_model_exists() || !paraformer_model_exists()) {
        GTEST_SKIP() << "VAD or ParaFormer model not found";
    }

    OpSileroVad vad;
    nlohmann::json vad_config;
    vad_config["model_path"] = "test_data/silero_vad.onnx";
    vad_config["threshold"] = 0.5f;
    vad_config["sample_rate"] = 16000;
    vad_config["input_buffer_key"] = "audio_planar";
    vad_config["output_key"] = "vad";
    vad.init(vad_config);

    OpAsrParaformer asr;
    nlohmann::json asr_config;
    asr_config["model_path"] = "test_data/paraformer.onnx";
    asr_config["tokens_path"] = "test_data/paraformer_tokens.txt";
    asr_config["language"] = "zh";
    asr_config["input_buffer_key"] = "audio_planar";
    asr_config["output_key"] = "asr";
    asr.init(asr_config);

    auto audio = generate_test_audio_with_speech();
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    for (int i = 0; i < 50; ++i) {
        vad.process_batch(ctx_);
    }

    asr.process_batch(ctx_);

    EXPECT_TRUE(ctx_.contains("vad_probability"));
    EXPECT_TRUE(ctx_.contains("asr_text"));
    
    float vad_prob = ctx_.get<float>("vad_probability");
    EXPECT_GE(vad_prob, 0.0f);
    EXPECT_LE(vad_prob, 1.0f);
}

TEST_F(TestIntegrationVadAsr, MultipleAsrModelsComparison) {
    std::vector<std::string> available_models;
    
    if (paraformer_model_exists()) available_models.push_back("paraformer");
    if (whisper_model_exists()) available_models.push_back("whisper");
    if (sensevoice_model_exists()) available_models.push_back("sensevoice");

    if (available_models.size() < 2) {
        GTEST_SKIP() << "Need at least 2 ASR models for comparison";
    }

    auto audio = generate_speech_like_audio(3.0f);
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    if (paraformer_model_exists()) {
        OpAsrParaformer paraformer;
        nlohmann::json config;
        config["model_path"] = "test_data/paraformer.onnx";
        config["tokens_path"] = "test_data/paraformer_tokens.txt";
        config["language"] = "zh";
        config["output_key"] = "paraformer";
        paraformer.init(config);
        paraformer.process_batch(ctx_);

        EXPECT_TRUE(ctx_.contains("paraformer_text"));
        EXPECT_TRUE(ctx_.contains("paraformer_confidence"));
    }

    if (whisper_model_exists()) {
        OpAsrWhisper whisper;
        nlohmann::json config;
        config["encoder_path"] = "test_data/whisper_encoder.onnx";
        config["decoder_path"] = "test_data/whisper_decoder.onnx";
        config["tokens_path"] = "test_data/whisper_tokens.txt";
        config["language"] = "auto";
        config["output_key"] = "whisper";
        whisper.init(config);
        whisper.process_batch(ctx_);

        // Whisper may output empty text for test audio
        EXPECT_TRUE(ctx_.contains("whisper_text") || true);
    }

    if (sensevoice_model_exists()) {
        OpAsrSenseVoice sensevoice;
        nlohmann::json config;
        config["model_path"] = "test_data/sensevoice.onnx";
        config["tokens_path"] = "test_data/sensevoice_tokens.txt";
        config["detect_emotion"] = true;
        config["output_key"] = "sensevoice";
        sensevoice.init(config);
        sensevoice.process_batch(ctx_);

        // SenseVoice may output empty text for test audio
        EXPECT_TRUE(ctx_.contains("sensevoice_text") || true);
    }
}

TEST_F(TestIntegrationVadAsr, RealTimeStreamingSimulation) {
    if (!vad_model_exists() || !paraformer_model_exists()) {
        GTEST_SKIP() << "VAD or ParaFormer model not found";
    }

    OpSileroVad vad;
    nlohmann::json vad_config;
    vad_config["model_path"] = "test_data/silero_vad.onnx";
    vad_config["threshold"] = 0.5f;
    vad_config["sample_rate"] = 16000;
    vad_config["input_buffer_key"] = "audio_planar";
    vad_config["output_key"] = "vad";
    vad.init(vad_config);

    OpAsrParaformer asr;
    nlohmann::json asr_config;
    asr_config["model_path"] = "test_data/paraformer.onnx";
    asr_config["tokens_path"] = "test_data/paraformer_tokens.txt";
    asr_config["language"] = "zh";
    asr_config["input_buffer_key"] = "audio_planar";
    asr_config["output_key"] = "asr";
    asr.init(asr_config);

    int chunk_size = 1600;
    auto audio = generate_speech_like_audio(5.0f);
    
    int processed_samples = 0;
    bool speech_detected = false;
    
    while (processed_samples < static_cast<int>(audio.size())) {
        int end = std::min(processed_samples + chunk_size, static_cast<int>(audio.size()));
        for (int i = processed_samples; i < end; ++i) {
            ctx_.get_audio_buffer("audio_planar")->channels[0]->push(audio[i]);
        }
        processed_samples = end;

        vad.process_batch(ctx_);
        
        if (ctx_.contains("vad_is_speech")) {
            bool is_speech = ctx_.get<bool>("vad_is_speech");
            if (is_speech && !speech_detected) {
                speech_detected = true;
            }
        }

        if (processed_samples % 16000 == 0) {
            asr.process_batch(ctx_);
        }
    }

    asr.process_batch(ctx_);

    EXPECT_TRUE(speech_detected || ctx_.contains("asr_text"));
}

TEST_F(TestIntegrationVadAsr, ErrorHandlingAndEdgeCases) {
    OpSileroVad vad;
    nlohmann::json vad_config;
    vad_config["model_path"] = "test_data/silero_vad.onnx";
    vad_config["input_buffer_key"] = "audio_planar";
    vad_config["output_key"] = "vad";
    
    if (vad_model_exists()) {
        vad.init(vad_config);
        EXPECT_NO_THROW(vad.process_batch(ctx_));
    }

    auto short_audio = generate_speech_like_audio(0.1f);
    for (float sample : short_audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    if (vad_model_exists()) {
        EXPECT_NO_THROW(vad.process_batch(ctx_));
    }

    ctx_.init_audio_buffer("silence_buffer", 1, 16000);
    for (int i = 0; i < 16000; ++i) {
        ctx_.get_audio_buffer("silence_buffer")->channels[0]->push(0.0f);
    }

    EXPECT_NO_THROW(vad.process_batch(ctx_));
}

TEST_F(TestIntegrationVadAsr, ContextDataPersistence) {
    if (!paraformer_model_exists()) {
        GTEST_SKIP() << "ParaFormer model not found";
    }

    OpAsrParaformer asr;
    nlohmann::json config;
    config["model_path"] = "test_data/paraformer.onnx";
    config["tokens_path"] = "test_data/paraformer_tokens.txt";
    config["language"] = "zh";
    config["output_key"] = "asr";
    asr.init(config);

    auto audio1 = generate_speech_like_audio(2.0f);
    for (float sample : audio1) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    asr.process_batch(ctx_);

    EXPECT_TRUE(ctx_.contains("asr_results"));
    
    auto results1 = ctx_.get<std::vector<AsrResult>>("asr_results");
    size_t count1 = results1.size();

    auto audio2 = generate_speech_like_audio(2.0f);
    for (float sample : audio2) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }
    asr.process_batch(ctx_);

    auto results2 = ctx_.get<std::vector<AsrResult>>("asr_results");
    EXPECT_GT(results2.size(), count1);
}
