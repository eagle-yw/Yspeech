module;

#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

export module yspeech.op.asr.paraformer;

import std;
import yspeech.context;
import yspeech.op;
import yspeech.op.asr.base;
import yspeech.types;
import yspeech.log;

namespace yspeech {

export class OpAsrParaformer : public AsrBase {
public:
    OpAsrParaformer() = default;

    OpAsrParaformer(const OpAsrParaformer&) = delete;
    OpAsrParaformer& operator=(const OpAsrParaformer&) = delete;
    OpAsrParaformer(OpAsrParaformer&&) noexcept = default;
    OpAsrParaformer& operator=(OpAsrParaformer&&) noexcept = default;

    void init(const nlohmann::json& config) override {
        AsrBase::init(config);

        if (config.contains("model_path")) {
            model_path_ = config["model_path"].get<std::string>();
        }
        if (config.contains("feature_input_key")) {
            feature_input_key_ = config["feature_input_key"].get<std::string>();
        }
        if (config.contains("hotwords")) {
            hotwords_ = config["hotwords"].get<std::vector<std::string>>();
        }

        init_onnx_session();
        load_tokens();

        log_info("OpAsrParaformer initialized: model={}, tokens={}", model_path_, tokens_path_);
    }

    void process(Context& ctx) override {
        std::vector<std::vector<float>> features;
        
        if (ctx.contains(feature_input_key_ + "_features")) {
            features = ctx.get<std::vector<std::vector<float>>>(feature_input_key_ + "_features");
            log_debug("Using features from {} with {} frames", feature_input_key_, features.size());
        } else {
            log_warn("No features found at {}_features", feature_input_key_);
            return;
        }

        if (features.empty()) {
            log_debug("Empty features");
            return;
        }

        AsrResult result = infer(features);

        ctx.set(output_key_ + "_text", result.text);
        ctx.set(output_key_ + "_confidence", result.confidence);
        ctx.set(output_key_ + "_language", result.language);

        std::vector<AsrResult> results;
        if (ctx.contains(output_key_ + "_results")) {
            results = ctx.get<std::vector<AsrResult>>(output_key_ + "_results");
        }
        results.push_back(result);
        ctx.set(output_key_ + "_results", results);

        log_debug("ASR result: \"{}\" (confidence={:.2f})", result.text, result.confidence);
    }

    void deinit() override {
        session_.reset();
        env_.reset();
        AsrBase::deinit();
    }

private:
    void init_onnx_session() {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "yspeech_paraformer");
        env_ = std::make_unique<Ort::Env>(std::move(env));

        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(num_threads_);
        session_options.SetInterOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = std::make_unique<Ort::Session>(*env_, model_path_.c_str(), session_options);
        memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        
        log_debug("ONNX session created for {}", model_path_);
        
        Ort::AllocatorWithDefaultOptions allocator;
        size_t num_inputs = session_->GetInputCount();
        log_debug("Model has {} inputs:", num_inputs);
        for (size_t i = 0; i < num_inputs; ++i) {
            auto name = session_->GetInputNameAllocated(i, allocator);
            auto type_info = session_->GetInputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();
            std::string shape_str;
            for (auto dim : shape) {
                shape_str += std::to_string(dim) + " ";
            }
            log_debug("  Input {}: {} [{}]", i, name.get(), shape_str);
        }
        
        size_t num_outputs = session_->GetOutputCount();
        log_debug("Model has {} outputs:", num_outputs);
        for (size_t i = 0; i < num_outputs; ++i) {
            auto name = session_->GetOutputNameAllocated(i, allocator);
            auto type_info = session_->GetOutputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();
            std::string shape_str;
            for (auto dim : shape) {
                shape_str += std::to_string(dim) + " ";
            }
            log_debug("  Output {}: {} [{}]", i, name.get(), shape_str);
        }
    }

    void load_tokens() {
        if (tokens_path_.empty()) {
            log_warn("No tokens path specified");
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
            if (!line.empty()) {
                size_t space_pos = line.find(' ');
                if (space_pos != std::string::npos) {
                    id_to_token_[idx++] = line.substr(0, space_pos);
                } else {
                    id_to_token_[idx++] = line;
                }
            }
        }

        log_debug("Loaded {} tokens from {}", id_to_token_.size(), tokens_path_);
    }

    AsrResult infer(const std::vector<std::vector<float>>& features) {
        AsrResult result;

        if (!session_ || features.empty()) {
            result.text = "";
            result.confidence = 0.0f;
            return result;
        }

        int num_frames = static_cast<int>(features.size());
        int feat_dim = static_cast<int>(features[0].size());

        log_debug("Inference: {} frames, {} dims", num_frames, feat_dim);

        std::vector<float> input_data;
        input_data.reserve(num_frames * feat_dim);
        for (const auto& frame : features) {
            input_data.insert(input_data.end(), frame.begin(), frame.end());
        }

        std::vector<int64_t> input_shape = {1, num_frames, feat_dim};
        std::vector<int64_t> length_shape = {1};
        std::vector<int32_t> length_data = {num_frames};

        log_debug("Creating input tensors...");

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info_, input_data.data(), input_data.size(), 
            input_shape.data(), input_shape.size());
        
        Ort::Value length_tensor = Ort::Value::CreateTensor<int32_t>(
            memory_info_, length_data.data(), length_data.size(),
            length_shape.data(), length_shape.size());

        std::array<Ort::Value, 2> input_tensors = {std::move(input_tensor), std::move(length_tensor)};

        std::vector<const char*> input_names = {"speech", "speech_lengths"};
        std::vector<const char*> output_names = {"logits", "token_num"};

        log_debug("Running ONNX inference...");

        try {
            auto outputs = session_->Run(
                Ort::RunOptions{nullptr},
                input_names.data(),
                input_tensors.data(),
                2,
                output_names.data(),
                output_names.size()
            );

            log_debug("Inference complete, processing outputs...");

            float* logits_data = outputs[0].GetTensorMutableData<float>();
            auto logits_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
            int32_t token_num = outputs[1].GetTensorMutableData<int32_t>()[0];
            
            int output_frames = static_cast<int>(logits_shape[1]);
            int vocab_size = static_cast<int>(logits_shape[2]);
            
            std::vector<int> token_ids;
            token_ids.reserve(token_num);
            
            for (int t = 0; t < token_num && t < output_frames; ++t) {
                float* frame_logits = logits_data + t * vocab_size;
                int best_token = 0;
                float best_score = frame_logits[0];
                for (int v = 1; v < vocab_size; ++v) {
                    if (frame_logits[v] > best_score) {
                        best_score = frame_logits[v];
                        best_token = v;
                    }
                }
                if (best_token != 0 && best_token != 2) {
                    token_ids.push_back(best_token);
                }
            }
            
            std::string text;
            for (int tid : token_ids) {
                auto it = id_to_token_.find(tid);
                if (it != id_to_token_.end()) {
                    text += it->second;
                }
            }
            
            result.text = text;
            result.confidence = 0.9f;
            result.language = language_;

        } catch (const Ort::Exception& e) {
            log_error("ONNX Runtime error: {}", e.what());
            result.text = "";
            result.confidence = 0.0f;
        }

        return result;
    }

    std::string model_path_;
    std::string feature_input_key_ = "fbank";
    std::vector<std::string> hotwords_;

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_{nullptr};

    std::unordered_map<int, std::string> id_to_token_;
};

namespace {

OperatorRegistrar<OpAsrParaformer> registrar("AsrParaformer");

}

}
