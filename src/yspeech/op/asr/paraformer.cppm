module;

#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

export module yspeech.op.asr.paraformer;

import std;
import yspeech.context;
import yspeech.op;
import yspeech.op.ort_symbol_lookup;
import yspeech.op.asr.base;
import yspeech.stream_store;
import yspeech.types;
import yspeech.log;
import yspeech.data_keys;

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
        if (config.contains("min_new_feature_frames")) {
            min_new_feature_frames_ = config["min_new_feature_frames"].get<int>();
        }
        if (config.contains("vad_input_key")) {
            vad_input_key_ = config["vad_input_key"].get<std::string>();
        }
        init_onnx_session();
        load_tokens();

        log_info("OpAsrParaformer initialized: model={}, tokens={}", model_path_, tokens_path_);
    }

    bool ready(Context& ctx, StreamStore&) {
        const auto feature_version = ctx.get_or_default<std::uint64_t>(feature_input_key_ + "_version", 0);
        const int current_frames = ctx.get_or_default<int>(feature_input_key_ + "_num_frames", 0);
        const auto segment_count = ctx.get_or_default<std::size_t>(vad_input_key_ + "_segment_count", 0);
        const bool has_partial_work =
            feature_version > last_feature_version_ &&
            ((current_frames - last_feature_count_) >= min_new_feature_frames_ || last_feature_version_ == 0);
        const bool has_segment_final_work =
            segment_count > finalized_segment_count_ && feature_version > 0;
        return has_partial_work || has_segment_final_work;
    }

    StreamProcessResult process_stream(Context& ctx, StreamStore&) override {
        const auto feature_version = ctx.get_or_default<std::uint64_t>(feature_input_key_ + "_version", 0);
        const auto segment_count = ctx.get_or_default<std::size_t>(vad_input_key_ + "_segment_count", 0);
        const bool should_emit_segment_final =
            segment_count > finalized_segment_count_ && feature_version > finalized_segment_feature_version_;
        if (feature_version <= last_feature_version_ && !should_emit_segment_final) {
            return {};
        }

        const auto* features = get_features_ref(ctx, feature_input_key_ + "_features");
        if (!features || features->empty()) {
            return {};
        }

        AsrResult result;
        if (feature_version == last_feature_version_ && last_result_cache_.has_value()) {
            result = *last_result_cache_;
        } else {
            const auto infer_start = std::chrono::steady_clock::now();
            result = infer(*features);
            const auto infer_end = std::chrono::steady_clock::now();
            const auto infer_ms = std::chrono::duration<double, std::milli>(infer_end - infer_start).count();
            ctx.record_operator_effective_sample(operator_id_, infer_ms);
        }
        if (result.text.empty()) {
            return {
                .status = StreamProcessStatus::NeedMoreInput
            };
        }

        ctx.set(output_key_ + "_text", result.text);
        ctx.set(output_key_ + "_confidence", result.confidence);
        ctx.set(output_key_ + "_language", result.language);

        std::vector<AsrResult> results = ctx.get_or_default(
            output_key_ + "_results", std::vector<AsrResult>{});
        results.push_back(result);
        ctx.set(output_key_ + "_results", std::move(results));

        auto events = ctx.get_or_default(output_key_ + "_events", std::vector<AsrEvent>{});
        events.push_back(AsrEvent{
            .kind = should_emit_segment_final ? AsrResultKind::SegmentFinal : AsrResultKind::Partial,
            .result = result,
            .segment = should_emit_segment_final && ctx.contains(vad_input_key_ + "_last_segment")
                ? std::optional<VadSegment>(ctx.get<VadSegment>(vad_input_key_ + "_last_segment"))
                : std::nullopt
        });
        ctx.set(output_key_ + "_events", std::move(events));

        last_feature_version_ = feature_version;
        last_feature_count_ = static_cast<int>(features->size());
        if (should_emit_segment_final) {
            finalized_segment_count_ = segment_count;
            finalized_segment_feature_version_ = feature_version;
        }
        finalized_stream_feature_version_ = 0;
        last_result_cache_ = result;

        return StreamProcessResult{
            .status = should_emit_segment_final ? StreamProcessStatus::SegmentFinalized
                                                : StreamProcessStatus::ProducedOutput,
            .produced_items = 1,
            .wake_downstream = true
        };
    }

    StreamProcessResult flush(Context& ctx, StreamStore&) override {
        const auto feature_version = ctx.get_or_default<std::uint64_t>(feature_input_key_ + "_version", 0);
        if (feature_version == finalized_stream_feature_version_) {
            return {};
        }

        const auto* features = get_features_ref(ctx, feature_input_key_ + "_features");
        if (!features || features->empty()) {
            return {};
        }

        AsrResult result;
        if (feature_version == last_feature_version_ && last_result_cache_.has_value()) {
            result = *last_result_cache_;
        } else {
            const auto infer_start = std::chrono::steady_clock::now();
            result = infer(*features);
            const auto infer_end = std::chrono::steady_clock::now();
            const auto infer_ms = std::chrono::duration<double, std::milli>(infer_end - infer_start).count();
            ctx.record_operator_effective_sample(operator_id_, infer_ms);
        }
        if (result.text.empty()) {
            return {};
        }

        ctx.set(output_key_ + "_text", result.text);
        ctx.set(output_key_ + "_confidence", result.confidence);
        ctx.set(output_key_ + "_language", result.language);

        std::vector<AsrResult> results = ctx.get_or_default(
            output_key_ + "_results", std::vector<AsrResult>{});
        results.push_back(result);
        ctx.set(output_key_ + "_results", std::move(results));

        auto events = ctx.get_or_default(output_key_ + "_events", std::vector<AsrEvent>{});
        events.push_back(AsrEvent{
            .kind = AsrResultKind::StreamFinal,
            .result = result,
            .segment = ctx.contains(vad_input_key_ + "_last_segment")
                ? std::optional<VadSegment>(ctx.get<VadSegment>(vad_input_key_ + "_last_segment"))
                : std::nullopt
        });
        ctx.set(output_key_ + "_events", std::move(events));

        last_feature_version_ = feature_version;
        last_feature_count_ = static_cast<int>(features->size());
        finalized_stream_feature_version_ = feature_version;
        last_result_cache_ = result;

        return StreamProcessResult{
            .status = StreamProcessStatus::StreamFinalized,
            .produced_items = 1,
            .wake_downstream = true
        };
    }

    void deinit() override {
        log_infer_profile();
        session_.reset();
        env_.reset();
        input_buffer_.clear();
        last_result_cache_.reset();
        infer_latency_ms_.clear();
        infer_total_ms_ = 0.0;
        infer_pack_ms_ = 0.0;
        infer_run_ms_ = 0.0;
        infer_decode_ms_ = 0.0;
        infer_calls_ = 0;
        AsrBase::deinit();
    }

private:
    static auto get_features_ref(Context& ctx, const std::string& key)
        -> const std::vector<std::vector<float>>* {
        if (!ctx.contains(key)) {
            return nullptr;
        }
        try {
            return &ctx.get<std::vector<std::vector<float>>>(key);
        } catch (const std::exception&) {
            return nullptr;
        }
    }

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

    void append_coreml_provider(Ort::SessionOptions& session_options) {
        if (!use_coreml_) {
            return;
        }
        std::uint32_t flags = coreml_flags_;
        if (coreml_ane_only_) {
            flags |= 0x004; // COREML_FLAG_ONLY_ENABLE_DEVICE_WITH_ANE
        }
        using AppendCoreMLProviderFn = OrtStatus* (*)(OrtSessionOptions*, std::uint32_t);
        auto* append_coreml_provider = ort_detail::lookup_symbol_fn<AppendCoreMLProviderFn>(
            "OrtSessionOptionsAppendExecutionProvider_CoreML");
        if (append_coreml_provider == nullptr) {
            log_warn("CoreML EP requested for paraformer, but symbol 'OrtSessionOptionsAppendExecutionProvider_CoreML' is unavailable. Falling back to CPU.");
            return;
        }
        auto* status = append_coreml_provider(session_options, flags);
        if (status != nullptr) {
            const char* msg = Ort::GetApi().GetErrorMessage(status);
            log_warn("CoreML EP requested but unavailable for paraformer: {}. Falling back to CPU.", msg ? msg : "unknown");
            Ort::GetApi().ReleaseStatus(status);
            return;
        }
        log_info("Paraformer using CoreML EP (flags={})", flags);
    }

    void log_infer_profile() {
        if (infer_calls_ == 0) {
            return;
        }
        auto samples = infer_latency_ms_;
        std::sort(samples.begin(), samples.end());
        const auto pick = [&](double q) {
            const auto idx = static_cast<std::size_t>(
                std::clamp<double>(q * static_cast<double>(samples.size() - 1), 0.0, static_cast<double>(samples.size() - 1)));
            return samples[idx];
        };
        const double avg = infer_total_ms_ / static_cast<double>(infer_calls_);
        log_info(
            "Paraformer infer profile: calls={}, total={:.2f}ms, avg={:.2f}ms, p95={:.2f}ms, p99={:.2f}ms, pack={:.2f}ms, run={:.2f}ms, decode={:.2f}ms",
            infer_calls_, infer_total_ms_, avg, pick(0.95), pick(0.99), infer_pack_ms_, infer_run_ms_, infer_decode_ms_);
    }

    AsrResult infer(const std::vector<std::vector<float>>& features) {
        using clock = std::chrono::steady_clock;
        const auto infer_start = clock::now();
        AsrResult result;

        if (!session_ || features.empty()) {
            result.text = "";
            result.confidence = 0.0f;
            return result;
        }

        int num_frames = static_cast<int>(features.size());
        int feat_dim = static_cast<int>(features[0].size());

        log_debug("Inference: {} frames, {} dims", num_frames, feat_dim);

        const auto flattened_size = static_cast<std::size_t>(num_frames * feat_dim);
        input_buffer_.resize(flattened_size);
        float* dst = input_buffer_.data();
        for (const auto& frame : features) {
            std::copy(frame.begin(), frame.end(), dst);
            dst += feat_dim;
        }
        const auto after_pack = clock::now();

        const std::array<int64_t, 3> input_shape = {1, num_frames, feat_dim};
        const std::array<int64_t, 1> length_shape = {1};
        std::array<int32_t, 1> length_data = {num_frames};

        log_debug("Creating input tensors...");

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info_, input_buffer_.data(), input_buffer_.size(),
            input_shape.data(), input_shape.size());
        
        Ort::Value length_tensor = Ort::Value::CreateTensor<int32_t>(
            memory_info_, length_data.data(), length_data.size(),
            length_shape.data(), length_shape.size());

        std::array<Ort::Value, 2> input_tensors = {std::move(input_tensor), std::move(length_tensor)};

        static constexpr std::array<const char*, 2> input_names = {"speech", "speech_lengths"};
        static constexpr std::array<const char*, 2> output_names = {"logits", "token_num"};

        log_debug("Running ONNX inference...");

        try {
            auto outputs = session_->Run(
                Ort::RunOptions{nullptr},
                input_names.data(),
                input_tensors.data(),
                input_names.size(),
                output_names.data(),
                output_names.size()
            );
            const auto after_run = clock::now();

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
            const auto infer_end = clock::now();
            const auto pack_ms = std::chrono::duration<double, std::milli>(after_pack - infer_start).count();
            const auto run_ms = std::chrono::duration<double, std::milli>(after_run - after_pack).count();
            const auto decode_ms = std::chrono::duration<double, std::milli>(infer_end - after_run).count();
            const auto total_ms = pack_ms + run_ms + decode_ms;
            infer_pack_ms_ += pack_ms;
            infer_run_ms_ += run_ms;
            infer_decode_ms_ += decode_ms;
            infer_total_ms_ += total_ms;
            ++infer_calls_;
            infer_latency_ms_.push_back(total_ms);

        } catch (const Ort::Exception& e) {
            log_error("ONNX Runtime error: {}", e.what());
            result.text = "";
            result.confidence = 0.0f;
        }

        return result;
    }

    std::string model_path_;
    std::string feature_input_key_ = "fbank";
    std::string vad_input_key_ = "vad";
    std::vector<std::string> hotwords_;

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_{nullptr};

    std::unordered_map<int, std::string> id_to_token_;
    std::vector<float> input_buffer_;
    std::optional<AsrResult> last_result_cache_;
    std::vector<double> infer_latency_ms_;
    double infer_total_ms_ = 0.0;
    double infer_pack_ms_ = 0.0;
    double infer_run_ms_ = 0.0;
    double infer_decode_ms_ = 0.0;
    std::size_t infer_calls_ = 0;
    int min_new_feature_frames_ = 8;
    std::uint64_t last_feature_version_ = 0;
    int last_feature_count_ = 0;
    std::size_t finalized_segment_count_ = 0;
    std::uint64_t finalized_segment_feature_version_ = 0;
    std::uint64_t finalized_stream_feature_version_ = 0;
};

namespace {

OperatorRegistrar<OpAsrParaformer> registrar("AsrParaformer");

}

}
