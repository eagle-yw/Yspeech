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
        min_partial_frames_ = std::max(1, config.value("min_partial_frames", min_partial_frames_));
        init_onnx_session();
        load_tokens();
    }

    auto infer(const FeatureSequenceView& features) -> AsrResult override {
        return infer_with_mode(features, "infer");
    }

    auto decode_partial(const std::string& stream_id, const FeatureSequenceView& full_context) -> AsrResult override {
        if (auto state_features = feature_view_from_stream_state(stream_id); !state_features.empty()) {
            return infer_with_mode(state_features, "partial");
        }
        return infer_with_mode(full_context, "partial");
    }

    auto decode_final(const std::string& stream_id, const FeatureSequenceView& full_context) -> AsrResult override {
        (void)stream_id;
        return infer_with_mode(full_context, "final");
    }

private:
    struct StreamState {
        std::shared_ptr<FeatureChunkList> chunks = std::make_shared<FeatureChunkList>();
        int feature_count = 0;
    };

    static auto is_recoverable_shape_error(std::string_view message) -> bool {
        return message.find("The input tensor cannot be reshaped to the requested shape") != std::string_view::npos ||
               message.find("Input shape:{0,512}") != std::string_view::npos ||
               message.find("input_shape_size == size was false") != std::string_view::npos ||
               message.find("Reshape_6679") != std::string_view::npos ||
               message.find("Loop_6591") != std::string_view::npos;
    }

    static auto count_frames(const FeatureChunkListPtr& chunks) -> int {
        if (!chunks) {
            return 0;
        }

        int total_frames = 0;
        for (const auto& chunk : *chunks) {
            if (!chunk || chunk->empty()) {
                continue;
            }
            total_frames += static_cast<int>(chunk->size());
        }
        return total_frames;
    }

    auto feature_view_from_stream_state(const std::string& stream_id) const -> FeatureSequenceView {
        auto it = stream_states_.find(stream_id);
        if (it == stream_states_.end() || !it->second.chunks) {
            return {};
        }

        const int actual_frames = count_frames(it->second.chunks);
        if (actual_frames <= 0) {
            return {};
        }

        return FeatureSequenceView::from_chunk_list(it->second.chunks, actual_frames);
    }

    void ensure_unique_stream_chunks(StreamState& state) {
        if (!state.chunks) {
            state.chunks = std::make_shared<FeatureChunkList>();
            return;
        }
        if (state.chunks.use_count() > 1) {
            state.chunks = std::make_shared<FeatureChunkList>(*state.chunks);
        }
    }

    auto append_non_empty_chunks(StreamState& state, const FeatureChunkListPtr& chunks) -> int {
        if (!chunks) {
            return 0;
        }

        ensure_unique_stream_chunks(state);

        int appended_frames = 0;
        for (const auto& chunk : *chunks) {
            if (!chunk || chunk->empty()) {
                continue;
            }
            state.chunks->push_back(chunk);
            appended_frames += static_cast<int>(chunk->size());
        }
        return appended_frames;
    }

    auto pack_features(const FeatureSequenceView& features, int feat_dim) -> int {
        input_buffer_.clear();
        if (!features.chunks || feat_dim <= 0) {
            return 0;
        }

        input_buffer_.reserve(static_cast<std::size_t>(std::max(features.feature_count, 0) * feat_dim));
        int packed_frames = 0;
        for (const auto& chunk : *features.chunks) {
            if (!chunk || chunk->empty()) {
                continue;
            }
            for (const auto& frame : *chunk) {
                input_buffer_.insert(input_buffer_.end(), frame.begin(), frame.end());
                ++packed_frames;
            }
        }
        return packed_frames;
    }

    auto infer_with_mode(const FeatureSequenceView& features, std::string_view mode) -> AsrResult {
        using clock = std::chrono::steady_clock;
        const auto infer_start = clock::now();
        AsrResult result;

        if (!session_ || features.empty() || !features.chunks) {
            result.text = "";
            result.confidence = 0.0f;
            return result;
        }

        int feat_dim = features.feature_dim();
        if (feat_dim <= 0) {
            result.text = "";
            result.confidence = 0.0f;
            return result;
        }

        const int packed_frames = pack_features(features, feat_dim);
        if (packed_frames <= 0) {
            result.text = "";
            result.confidence = 0.0f;
            return result;
        }

        if (mode == "partial" && packed_frames < min_partial_frames_) {
            result.text = "";
            result.confidence = 0.0f;
            return result;
        }

        const auto after_pack = clock::now();

        const std::array<int64_t, 3> input_shape = {1, packed_frames, feat_dim};
        const std::array<int64_t, 1> length_shape = {1};
        std::array<int32_t, 1> length_data = {packed_frames};

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
            if (is_recoverable_shape_error(e.what())) {
                log_debug("Skipping paraformer {} decode for degenerate context: {}", mode, e.what());
            } else {
                log_error("ONNX Runtime error: {}", e.what());
            }
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
        state.feature_count += append_non_empty_chunks(state, delta_features.chunks);
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
    void init_onnx_session() {
        Ort::Env env(ORT_LOGGING_LEVEL_FATAL, "yspeech_paraformer");
        env_ = std::make_unique<Ort::Env>(std::move(env));
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(num_threads_);
        session_options.SetInterOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options.SetLogSeverityLevel(4);
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
    int min_partial_frames_ = 8;
    ProcessingStats* runtime_stats_ = nullptr;
};

AsrCoreRegistrar<ParaformerCore> paraformer_core_registrar("AsrParaformer");

}
