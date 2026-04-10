module;

#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

export module yspeech.domain.asr.paraformer;

import std;
import yspeech.onnx.ort_symbol_lookup;
import yspeech.domain.asr.base;
import yspeech.types;
import yspeech.log;

namespace yspeech {

export class ParaformerCore : public AsrBase, public AsrCoreIface {
public:
    void init(const nlohmann::json& config) override {
        AsrBase::init(config);
        if (config.contains("model_path")) {
            model_path_ = config["model_path"].get<std::string>();
        }
        init_onnx_session();
        load_tokens();
    }

    auto infer(const FeatureSequenceView& features) -> AsrResult override {
        return infer_with_mode(features, "infer");
    }

    auto decode_partial(const std::string& stream_id, const FeatureSequenceView& full_context) -> AsrResult override {
        if (auto it = stream_states_.find(stream_id);
            it != stream_states_.end() && it->second.chunks && !it->second.chunks->empty()) {
            return infer_with_mode(FeatureSequenceView::from_chunk_list(it->second.chunks, it->second.feature_count), "partial");
        }
        return infer_with_mode(full_context, "partial");
    }

    auto decode_final(const std::string& stream_id, const FeatureSequenceView& full_context) -> AsrResult override {
        (void)stream_id;
        return infer_with_mode(full_context, "final");
    }

private:
    auto infer_with_mode(const FeatureSequenceView& features, std::string_view mode) -> AsrResult {
        using clock = std::chrono::steady_clock;
        const auto infer_start = clock::now();
        AsrResult result;

        if (!session_ || features.empty()) {
            result.text = "";
            result.confidence = 0.0f;
            return result;
        }

        int num_frames = features.feature_count;
        int feat_dim = features.feature_dim();
        if (num_frames <= 0 || feat_dim <= 0) {
            result.text = "";
            result.confidence = 0.0f;
            return result;
        }
        const auto flattened_size = static_cast<std::size_t>(num_frames * feat_dim);
        input_buffer_.resize(flattened_size);
        float* dst = input_buffer_.data();
        for (const auto& chunk : *features.chunks) {
            if (!chunk || chunk->empty()) {
                continue;
            }
            for (const auto& frame : *chunk) {
                std::copy(frame.begin(), frame.end(), dst);
                dst += feat_dim;
            }
        }
        const auto after_pack = clock::now();

        const std::array<int64_t, 3> input_shape = {1, num_frames, feat_dim};
        const std::array<int64_t, 1> length_shape = {1};
        std::array<int32_t, 1> length_data = {num_frames};

        try {
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                memory_info_, input_buffer_.data(), input_buffer_.size(),
                input_shape.data(), input_shape.size());
            Ort::Value length_tensor = Ort::Value::CreateTensor<int32_t>(
                memory_info_, length_data.data(), length_data.size(),
                length_shape.data(), length_shape.size());
            std::array<Ort::Value, 2> input_tensors = {std::move(input_tensor), std::move(length_tensor)};
            static constexpr std::array<const char*, 2> input_names = {"speech", "speech_lengths"};
            static constexpr std::array<const char*, 2> output_names = {"logits", "token_num"};
            auto outputs = session_->Run(
                Ort::RunOptions{nullptr},
                input_names.data(), input_tensors.data(), input_names.size(),
                output_names.data(), output_names.size()
            );
            const auto after_run = clock::now();

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
                if (auto it = id_to_token_.find(tid); it != id_to_token_.end()) {
                    text += it->second;
                }
            }
            result.text = text;
            result.confidence = 0.9f;
            result.language = language_;

            const auto infer_end = clock::now();
            const auto total_ms = std::chrono::duration<double, std::milli>(infer_end - infer_start).count();
            const auto pack_ms = std::chrono::duration<double, std::milli>(after_pack - infer_start).count();
            const auto run_ms = std::chrono::duration<double, std::milli>(after_run - after_pack).count();
            const auto decode_ms = std::chrono::duration<double, std::milli>(infer_end - after_run).count();
            infer_latency_ms_.push_back(total_ms);
            infer_calls_++;
            infer_total_ms_ += infer_latency_ms_.back();
            infer_pack_ms_ += pack_ms;
            infer_run_ms_ += run_ms;
            infer_decode_ms_ += decode_ms;
            if (runtime_stats_ != nullptr) {
                runtime_stats_->record_core_phase_time(core_id_, std::format("pack_{}", mode), pack_ms);
                runtime_stats_->record_core_phase_time(core_id_, std::format("run_{}", mode), run_ms);
                runtime_stats_->record_core_phase_time(core_id_, std::format("decode_{}", mode), decode_ms);
            }
        } catch (const Ort::Exception& e) {
            log_error("ONNX Runtime error: {}", e.what());
            result.text.clear();
            result.confidence = 0.0f;
        }
        return result;
    }

    void bind_stats(ProcessingStats* stats) override {
        runtime_stats_ = stats;
    }

public:
    auto supports_incremental() const -> bool override {
        return true;
    }

    void accept_features(const std::string& stream_id, const FeatureSequenceView& delta_features) override {
        if (delta_features.empty() || !delta_features.chunks) {
            return;
        }
        auto& state = stream_states_[stream_id];
        if (!state.chunks) {
            state.chunks = std::make_shared<FeatureChunkList>();
        } else if (state.chunks.use_count() > 1) {
            state.chunks = std::make_shared<FeatureChunkList>(*state.chunks);
        }
        for (const auto& chunk : *delta_features.chunks) {
            if (!chunk || chunk->empty()) {
                continue;
            }
            state.chunks->push_back(chunk);
        }
        state.feature_count += delta_features.feature_count;
    }

    void reset_stream(const std::string& stream_id) override {
        stream_states_.erase(stream_id);
    }

    void deinit() override {
        session_.reset();
        env_.reset();
        input_buffer_.clear();
        stream_states_.clear();
        infer_latency_ms_.clear();
        infer_total_ms_ = 0.0;
        infer_pack_ms_ = 0.0;
        infer_run_ms_ = 0.0;
        infer_decode_ms_ = 0.0;
        infer_calls_ = 0;
    }

private:
    struct StreamState {
        std::shared_ptr<FeatureChunkList> chunks = std::make_shared<FeatureChunkList>();
        int feature_count = 0;
    };

    void init_onnx_session() {
        Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "yspeech_paraformer");
        env_ = std::make_unique<Ort::Env>(std::move(env));
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(num_threads_);
        session_options.SetInterOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        append_coreml_provider(session_options);
        session_ = std::make_unique<Ort::Session>(*env_, model_path_.c_str(), session_options);
        memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    }

    void load_tokens() {
        if (tokens_path_.empty()) {
            return;
        }
        std::ifstream file(tokens_path_);
        if (!file.is_open()) {
            return;
        }
        std::string line;
        int idx = 0;
        while (std::getline(file, line)) {
            line.erase(line.find_last_not_of(" \t\n\r") + 1);
            if (!line.empty()) {
                size_t space_pos = line.find(' ');
                id_to_token_[idx++] = space_pos != std::string::npos ? line.substr(0, space_pos) : line;
            }
        }
    }

    void append_coreml_provider(Ort::SessionOptions& session_options) {
        if (!use_coreml_) {
            return;
        }
        std::uint32_t flags = coreml_flags_;
        if (coreml_ane_only_) {
            flags |= 0x004;
        }
        using AppendCoreMLProviderFn = OrtStatus* (*)(OrtSessionOptions*, std::uint32_t);
        auto* append_coreml_provider = ort_detail::lookup_symbol_fn<AppendCoreMLProviderFn>(
            "OrtSessionOptionsAppendExecutionProvider_CoreML");
        if (append_coreml_provider == nullptr) {
            return;
        }
        auto* status = append_coreml_provider(session_options, flags);
        if (status != nullptr) {
            Ort::GetApi().ReleaseStatus(status);
        }
    }

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_{nullptr};
    std::unordered_map<int, std::string> id_to_token_;
    std::vector<float> input_buffer_;
    std::unordered_map<std::string, StreamState> stream_states_;
    std::vector<double> infer_latency_ms_;
    double infer_total_ms_ = 0.0;
    double infer_pack_ms_ = 0.0;
    double infer_run_ms_ = 0.0;
    double infer_decode_ms_ = 0.0;
    std::size_t infer_calls_ = 0;
    ProcessingStats* runtime_stats_ = nullptr;
};

AsrCoreRegistrar<ParaformerCore> paraformer_core_registrar("AsrParaformer");

}
