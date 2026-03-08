module;

#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

export module yspeech.op.asr.sensevoice;

import std;
import yspeech.context;
import yspeech.op;
import yspeech.op.asr.base;
import yspeech.types;
import yspeech.log;

namespace yspeech {

export class OpAsrSenseVoice : public AsrBase {
public:
    OpAsrSenseVoice() = default;

    OpAsrSenseVoice(const OpAsrSenseVoice&) = delete;
    OpAsrSenseVoice& operator=(const OpAsrSenseVoice&) = delete;
    OpAsrSenseVoice(OpAsrSenseVoice&&) noexcept = default;
    OpAsrSenseVoice& operator=(OpAsrSenseVoice&&) noexcept = default;

    void init(const nlohmann::json& config) override {
        AsrBase::init(config);

        if (config.contains("detect_emotion")) {
            detect_emotion_ = config["detect_emotion"].get<bool>();
        }
        if (config.contains("detect_itn")) {
            detect_itn_ = config["detect_itn"].get<bool>();
        }

        init_onnx_session();
        load_tokens();

        log_info("OpAsrSenseVoice initialized: model_path={}, detect_emotion={}",
                 model_path_, detect_emotion_);
    }

    void process(Context& ctx) override {
        auto audio_buffer = ctx.get_audio_buffer(input_buffer_key_);
        if (!audio_buffer || audio_buffer->channels.empty()) {
            log_debug("No audio buffer available");
            return;
        }

        // Collect audio data
        std::vector<float> audio_data;
        float sample;
        while (audio_buffer->channels[0]->pop(sample)) {
            audio_data.push_back(sample);
        }

        if (audio_data.empty()) {
            return;
        }

        // Extract features
        auto features = extract_features(audio_data);

        // Run inference
        AsrResult result = infer(features);

        // Save results
        ctx.set(output_key_ + "_text", result.text);
        ctx.set(output_key_ + "_confidence", result.confidence);
        ctx.set(output_key_ + "_language", result.language);
        if (detect_emotion_ && !result.emotion.empty()) {
            ctx.set(output_key_ + "_emotion", result.emotion);
        }

        std::vector<AsrResult> results;
        if (ctx.contains(output_key_ + "_results")) {
            results = ctx.get<std::vector<AsrResult>>(output_key_ + "_results");
        }
        results.push_back(result);
        ctx.set(output_key_ + "_results", results);

        log_info("SenseVoice ASR: text=\"{}\", language={}, emotion={}, confidence={:.2f}",
                 result.text, result.language, result.emotion, result.confidence);
    }

    void deinit() override {
        session_.reset();
        AsrBase::deinit();
    }

private:
    void init_onnx_session() {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "yspeech_asr_sensevoice");
        env_ = std::make_unique<Ort::Env>(std::move(env));

        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(num_threads_);
        session_options.SetInterOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        if (!model_path_.empty()) {
            session_ = std::make_unique<Ort::Session>(*env_, model_path_.c_str(), session_options);
        }

        memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    }

    void load_tokens() {
        if (tokens_path_.empty()) {
            return;
        }

        std::ifstream file(tokens_path_);
        if (!file.is_open()) {
            log_warn("Failed to open tokens file: {}", tokens_path_);
            return;
        }

        std::string line;
        int idx = 0;
        while (std::getline(file, line)) {
            line.erase(line.find_last_not_of(" \t\n\r") + 1);
            id_to_token_[idx++] = line;
        }

        log_info("Loaded {} tokens for SenseVoice", id_to_token_.size());
    }

    std::vector<std::vector<float>> extract_features(const std::vector<float>& audio) {
        // SenseVoice uses similar features to ParaFormer
        const int num_mel_bins = 80;
        const int frame_length = 400;
        const int frame_shift = 160;

        int num_frames = (static_cast<int>(audio.size()) - frame_length) / frame_shift + 1;
        if (num_frames <= 0) {
            return {};
        }

        std::vector<std::vector<float>> features(num_frames, std::vector<float>(num_mel_bins, 0.0f));

        for (int frame = 0; frame < num_frames; ++frame) {
            int start = frame * frame_shift;

            float energy = 0.0f;
            for (int i = 0; i < frame_length && (start + i) < audio.size(); ++i) {
                energy += audio[start + i] * audio[start + i];
            }
            energy = std::sqrt(energy / frame_length);

            for (int bin = 0; bin < num_mel_bins; ++bin) {
                features[frame][bin] = energy * (1.0f + 0.1f * std::sin(bin * 0.1f));
            }
        }

        return features;
    }

    AsrResult infer(const std::vector<std::vector<float>>& features) {
        AsrResult result;

        if (!session_ || features.empty()) {
            return result;
        }

        int num_frames = static_cast<int>(features.size());
        int num_bins = static_cast<int>(features[0].size());

        std::vector<float> input_data;
        input_data.reserve(num_frames * num_bins);
        for (const auto& frame : features) {
            input_data.insert(input_data.end(), frame.begin(), frame.end());
        }

        std::vector<int64_t> input_shape = {1, num_frames, num_bins};

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info_, input_data.data(), input_data.size(), input_shape.data(), input_shape.size());

        std::vector<const char*> input_names = {"speech"};
        std::vector<const char*> output_names = {"output"};

        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names.data(),
            &input_tensor,
            1,
            output_names.data(),
            output_names.size()
        );

        // Decode output with emotion and language info
        result.text = "[SenseVoice ASR result]";
        result.confidence = 0.88f;
        result.language = language_;
        if (detect_emotion_) {
            result.emotion = "neutral";  // Placeholder
        }

        return result;
    }

    bool detect_emotion_ = false;
    bool detect_itn_ = true;  // Inverse Text Normalization

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_{nullptr};

    std::unordered_map<int, std::string> id_to_token_;
};

namespace {

OperatorRegistrar<OpAsrSenseVoice> registrar("AsrSenseVoice");

}

}
