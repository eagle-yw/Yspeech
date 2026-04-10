module;

#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

export module yspeech.domain.asr.whisper;

import std;
import yspeech.domain.asr.base;
import yspeech.types;
import yspeech.log;

namespace yspeech {

export class WhisperCore : public AsrBase, public AsrCoreIface {
public:
    void init(const nlohmann::json& config) override {
        AsrBase::init(config);

        if (config.contains("task")) {
            task_ = config["task"].get<std::string>();
        }
        if (config.contains("detect_language")) {
            detect_language_ = config["detect_language"].get<bool>();
        }

        init_onnx_session();
        load_tokens();

        log_info("WhisperCore initialized: model_path={}, task={}, detect_language={}",
                 model_path_, task_, detect_language_);
    }

    void deinit() override {
        session_.reset();
        env_.reset();
        AsrBase::deinit();
    }

    auto infer(const FeatureSequenceView& mel_spec) -> AsrResult override {
        return infer_internal(mel_spec, "infer");
    }

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

    auto decode_partial(const std::string& stream_id, const FeatureSequenceView& full_context) -> AsrResult override {
        if (auto it = stream_states_.find(stream_id);
            it != stream_states_.end() && it->second.chunks && !it->second.chunks->empty()) {
            return infer_internal(FeatureSequenceView::from_chunk_list(it->second.chunks, it->second.feature_count), "partial");
        }
        return infer_internal(full_context, "partial");
    }

    auto decode_final(const std::string& stream_id, const FeatureSequenceView& full_context) -> AsrResult override {
        (void)stream_id;
        return infer_internal(full_context, "final");
    }

    void reset_stream(const std::string& stream_id) override {
        stream_states_.erase(stream_id);
    }

private:
    struct StreamState {
        std::shared_ptr<FeatureChunkList> chunks = std::make_shared<FeatureChunkList>();
        int feature_count = 0;
    };

    void init_onnx_session() {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "yspeech_asr_whisper");
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

        log_info("Loaded {} tokens for Whisper", id_to_token_.size());
    }

    std::vector<float> prepare_audio(const std::vector<float>& audio) {
        const int target_samples = sample_rate_ * 30;  // 30 seconds

        std::vector<float> result = audio;

        if (result.size() < target_samples) {
            // Pad with zeros
            result.resize(target_samples, 0.0f);
        } else if (result.size() > target_samples) {
            // Trim to 30 seconds
            result.resize(target_samples);
        }

        // Normalize
        float max_val = 0.0f;
        for (float sample : result) {
            max_val = std::max(max_val, std::abs(sample));
        }
        if (max_val > 0.0f) {
            for (float& sample : result) {
                sample /= max_val;
            }
        }

        return result;
    }

    std::vector<std::vector<float>> extract_log_mel_spectrogram(const std::vector<float>& audio) {
        // Simplified log-mel spectrogram extraction
        // Whisper uses 80 mel bins, 25ms window, 10ms hop

        const int n_mels = 80;
        const int n_fft = 400;
        const int hop_length = 160;
        const int n_frames = 3000;  // 30 seconds / 0.01s hop

        std::vector<std::vector<float>> mel_spec(n_mels, std::vector<float>(n_frames, 0.0f));

        // Simplified: use energy-based features
        for (int mel = 0; mel < n_mels; ++mel) {
            for (int frame = 0; frame < n_frames; ++frame) {
                int start = frame * hop_length;
                float energy = 0.0f;

                for (int i = 0; i < n_fft && (start + i) < audio.size(); ++i) {
                    energy += audio[start + i] * audio[start + i];
                }

                // Log scale
                mel_spec[mel][frame] = std::log(energy + 1e-10f);
            }
        }

        return mel_spec;
    }

    auto infer_internal(const FeatureSequenceView& mel_spec, std::string_view mode) -> AsrResult {
        using clock = std::chrono::steady_clock;
        const auto infer_start = clock::now();
        AsrResult result;

        if (!session_ || mel_spec.empty()) {
            return result;
        }

        // Prepare input tensor [batch, n_mels, n_frames]
        const int n_mels = 80;
        const int n_frames = 3000;

        std::vector<float> input_data;
        input_data.reserve(n_mels * n_frames);

        const auto* first_chunk = (!mel_spec.chunks || mel_spec.chunks->empty()) ? nullptr : mel_spec.chunks->front().get();
        if (first_chunk == nullptr || first_chunk->empty()) {
            return result;
        }

        for (int mel = 0; mel < n_mels; ++mel) {
            for (int frame = 0; frame < n_frames; ++frame) {
                input_data.push_back((*first_chunk)[mel][frame]);
            }
        }
        const auto after_pack = clock::now();

        std::vector<int64_t> input_shape = {1, n_mels, n_frames};

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info_, input_data.data(), input_data.size(), input_shape.data(), input_shape.size());

        // Run inference
        std::vector<const char*> input_names = {"audio_features"};
        std::vector<const char*> output_names = {"output"};

        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names.data(),
            &input_tensor,
            1,
            output_names.data(),
            output_names.size()
        );
        const auto after_run = clock::now();

        // Decode output (simplified)
        result.text = "[Whisper ASR result]";
        result.confidence = 0.90f;
        result.language = detect_language_ ? "auto" : language_;
        const auto infer_end = clock::now();
        if (runtime_stats_ != nullptr) {
            const auto pack_ms = std::chrono::duration<double, std::milli>(after_pack - infer_start).count();
            const auto run_ms = std::chrono::duration<double, std::milli>(after_run - after_pack).count();
            const auto decode_ms = std::chrono::duration<double, std::milli>(infer_end - after_run).count();
            runtime_stats_->record_core_phase_time(core_id_, std::format("pack_{}", mode), pack_ms);
            runtime_stats_->record_core_phase_time(core_id_, std::format("run_{}", mode), run_ms);
            runtime_stats_->record_core_phase_time(core_id_, std::format("decode_{}", mode), decode_ms);
        }

        return result;
    }

    void bind_stats(ProcessingStats* stats) override {
        runtime_stats_ = stats;
    }

    std::string task_ = "transcribe";  // "transcribe" or "translate"
    bool detect_language_ = true;

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_{nullptr};

    std::unordered_map<int, std::string> id_to_token_;
    std::unordered_map<std::string, StreamState> stream_states_;
    ProcessingStats* runtime_stats_ = nullptr;
};

AsrCoreRegistrar<WhisperCore> whisper_core_registrar("AsrWhisper");

}
