#include "gtest/gtest.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cmath>

import yspeech.context;
import yspeech.op.vad.silero;
import yspeech.op.asr.paraformer;
import yspeech.op.asr.whisper;
import yspeech.op.asr.sensevoice;

using namespace yspeech;

// 简单的线性重采样：从 source_rate 到 target_rate
std::vector<float> resample_audio(const std::vector<float>& input, int source_rate, int target_rate) {
    if (source_rate == target_rate) {
        return input;
    }

    double ratio = static_cast<double>(target_rate) / source_rate;
    size_t output_size = static_cast<size_t>(input.size() * ratio);
    std::vector<float> output;
    output.reserve(output_size);

    for (size_t i = 0; i < output_size; ++i) {
        double src_idx = i / ratio;
        size_t idx_low = static_cast<size_t>(src_idx);
        size_t idx_high = std::min(idx_low + 1, input.size() - 1);
        double frac = src_idx - idx_low;

        float val = input[idx_low] * (1.0 - frac) + input[idx_high] * frac;
        output.push_back(val);
    }

    return output;
}

// 加载 WAV 文件的简单实现（支持 24kHz -> 16kHz 重采样）
std::vector<float> load_wav_file(const std::string& filepath, int target_sample_rate = 16000) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    // 读取 WAV 头信息
    file.seekg(24);  // 跳到采样率位置
    int32_t sample_rate;
    file.read(reinterpret_cast<char*>(&sample_rate), 4);

    // 跳过剩余头部到数据区
    file.seekg(44);

    // 读取 PCM 数据 (16-bit)
    std::vector<int16_t> pcm_data;
    int16_t sample;
    while (file.read(reinterpret_cast<char*>(&sample), sizeof(sample))) {
        pcm_data.push_back(sample);
    }

    // 转换为 float (-1.0 to 1.0)
    std::vector<float> audio_data;
    for (int16_t s : pcm_data) {
        audio_data.push_back(static_cast<float>(s) / 32768.0f);
    }

    // 重采样到目标采样率
    if (sample_rate != target_sample_rate) {
        std::cout << "Resampling from " << sample_rate << "Hz to " << target_sample_rate << "Hz" << std::endl;
        audio_data = resample_audio(audio_data, sample_rate, target_sample_rate);
    }

    return audio_data;
}

class TestAsrRealAudio : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.init_audio_buffer("audio_planar", 1, 16000 * 60);
    }

    bool has_test_audio() const {
        // 检查是否存在测试音频文件
        std::ifstream f1("test_data/test_zh.wav");
        std::ifstream f2("test_data/test_en.wav");
        return f1.good() || f2.good();
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
        return file.good();
    }

    Context ctx_;
};

// 使用真实中文语音测试 ParaFormer
TEST_F(TestAsrRealAudio, ParaFormerRecognizeChinese) {
    if (!paraformer_model_exists()) {
        GTEST_SKIP() << "ParaFormer model not found";
    }

    // 加载测试音频（中文语音）
    auto audio = load_wav_file("test_data/test_zh.wav");
    if (audio.empty()) {
        GTEST_SKIP() << "Test audio file not found: test_data/test_zh.wav";
    }

    // 初始化 ASR
    OpAsrParaformer asr;
    nlohmann::json config;
    config["model_path"] = "test_data/paraformer.onnx";
    config["tokens_path"] = "test_data/paraformer_tokens.txt";
    config["language"] = "zh";
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "asr";
    asr.init(config);

    // 加载音频数据
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    // 执行识别
    asr.process_batch(ctx_);

    // 输出识别结果
    std::string text = ctx_.get<std::string>("asr_text");
    float confidence = ctx_.get<float>("asr_confidence");

    std::cout << "\n========================================" << std::endl;
    std::cout << "ParaFormer 识别结果:" << std::endl;
    std::cout << "文本: " << text << std::endl;
    std::cout << "置信度: " << confidence << std::endl;
    std::cout << "========================================\n" << std::endl;

    // 验证有输出（不一定是特定文本，因为测试音频可能不同）
    EXPECT_FALSE(text.empty());
}

// 使用真实英文语音测试 Whisper
TEST_F(TestAsrRealAudio, WhisperRecognizeEnglish) {
    if (!whisper_model_exists()) {
        GTEST_SKIP() << "Whisper model not found";
    }

    // 加载测试音频（英文语音）
    auto audio = load_wav_file("test_data/test_en.wav");
    if (audio.empty()) {
        GTEST_SKIP() << "Test audio file not found: test_data/test_en.wav";
    }

    // 初始化 Whisper
    OpAsrWhisper asr;
    nlohmann::json config;
    config["encoder_path"] = "test_data/whisper_encoder.onnx";
    config["decoder_path"] = "test_data/whisper_decoder.onnx";
    config["tokens_path"] = "test_data/whisper_tokens.txt";
    config["language"] = "en";
    config["task"] = "transcribe";
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "asr";
    asr.init(config);

    // 加载音频数据
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    // 执行识别
    asr.process_batch(ctx_);

    // 输出识别结果
    std::string text = ctx_.get<std::string>("asr_text");
    std::string language = ctx_.get<std::string>("asr_language");
    float confidence = ctx_.get<float>("asr_confidence");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Whisper 识别结果:" << std::endl;
    std::cout << "文本: " << text << std::endl;
    std::cout << "检测语言: " << language << std::endl;
    std::cout << "置信度: " << confidence << std::endl;
    std::cout << "========================================\n" << std::endl;

    EXPECT_FALSE(text.empty());
}

// 使用真实语音测试 SenseVoice（带情感）
TEST_F(TestAsrRealAudio, SenseVoiceWithEmotion) {
    if (!sensevoice_model_exists()) {
        GTEST_SKIP() << "SenseVoice model not found";
    }

    // 加载测试音频
    auto audio = load_wav_file("test_data/test_zh.wav");
    if (audio.empty()) {
        GTEST_SKIP() << "Test audio file not found: test_data/test_zh.wav";
    }

    // 初始化 SenseVoice
    OpAsrSenseVoice asr;
    nlohmann::json config;
    config["model_path"] = "test_data/sensevoice.onnx";
    config["tokens_path"] = "test_data/sensevoice_tokens.txt";
    config["language"] = "zh";
    config["detect_emotion"] = true;
    config["input_buffer_key"] = "audio_planar";
    config["output_key"] = "asr";
    asr.init(config);

    // 加载音频数据
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    // 执行识别
    asr.process_batch(ctx_);

    // 输出识别结果
    std::string text = ctx_.get<std::string>("asr_text");
    std::string emotion = ctx_.contains("asr_emotion") ? 
                          ctx_.get<std::string>("asr_emotion") : "unknown";
    float confidence = ctx_.get<float>("asr_confidence");

    std::cout << "\n========================================" << std::endl;
    std::cout << "SenseVoice 识别结果:" << std::endl;
    std::cout << "文本: " << text << std::endl;
    std::cout << "情感: " << emotion << std::endl;
    std::cout << "置信度: " << confidence << std::endl;
    std::cout << "========================================\n" << std::endl;

    EXPECT_FALSE(text.empty());
}

// VAD + ASR 完整流程测试（使用真实音频）
TEST_F(TestAsrRealAudio, CompleteVadAsrPipeline) {
    if (!paraformer_model_exists()) {
        GTEST_SKIP() << "ParaFormer model not found";
    }

    // 加载测试音频
    auto audio = load_wav_file("test_data/test_zh.wav");
    if (audio.empty()) {
        GTEST_SKIP() << "Test audio file not found: test_data/test_zh.wav";
    }

    // 初始化 VAD
    OpSileroVad vad;
    nlohmann::json vad_config;
    vad_config["model_path"] = "test_data/silero_vad.onnx";
    vad_config["threshold"] = 0.5f;
    vad_config["sample_rate"] = 16000;
    vad_config["input_buffer_key"] = "audio_planar";
    vad_config["output_key"] = "vad";
    vad.init(vad_config);

    // 初始化 ASR
    OpAsrParaformer asr;
    nlohmann::json asr_config;
    asr_config["model_path"] = "test_data/paraformer.onnx";
    asr_config["tokens_path"] = "test_data/paraformer_tokens.txt";
    asr_config["language"] = "zh";
    asr_config["input_buffer_key"] = "audio_planar";
    asr_config["output_key"] = "asr";
    asr.init(asr_config);

    // 加载音频数据
    for (float sample : audio) {
        ctx_.get_audio_buffer("audio_planar")->channels[0]->push(sample);
    }

    // 执行 VAD
    for (int i = 0; i < 50; ++i) {
        vad.process_batch(ctx_);
    }

    // 执行 ASR
    asr.process_batch(ctx_);

    // 输出完整结果
    std::string text = ctx_.get<std::string>("asr_text");
    float confidence = ctx_.get<float>("asr_confidence");
    bool is_speech = ctx_.contains("vad_is_speech") ? 
                     ctx_.get<bool>("vad_is_speech") : false;

    std::cout << "\n========================================" << std::endl;
    std::cout << "完整 VAD + ASR 流程结果:" << std::endl;
    std::cout << "VAD 检测到语音: " << (is_speech ? "是" : "否") << std::endl;
    std::cout << "识别文本: " << text << std::endl;
    std::cout << "置信度: " << confidence << std::endl;
    std::cout << "========================================\n" << std::endl;

    EXPECT_FALSE(text.empty());
}
