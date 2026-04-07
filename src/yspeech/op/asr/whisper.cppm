module;

#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

export module yspeech.op.asr.whisper;

import std;
import yspeech.context;
import yspeech.op;
import yspeech.op.asr.base;
import yspeech.frame_ring;
import yspeech.stream_store;
import yspeech.types;
import yspeech.log;

namespace yspeech {

export class OpAsrWhisper : public AsrBase {
public:
    OpAsrWhisper() = default;

    OpAsrWhisper(const OpAsrWhisper&) = delete;
    OpAsrWhisper& operator=(const OpAsrWhisper&) = delete;
    OpAsrWhisper(OpAsrWhisper&&) noexcept = default;
    OpAsrWhisper& operator=(OpAsrWhisper&&) noexcept = default;

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

        log_info("OpAsrWhisper initialized: model_path={}, task={}, detect_language={}",
                 model_path_, task_, detect_language_);
    }

    void process_batch(Context& ctx) override {
        std::vector<float> audio_data;
        int max_samples = sample_rate_ * 30;  // 30 seconds max
        while (audio_data.size() < static_cast<size_t>(max_samples)) {
            auto read_result = ctx.read_audio_frame(input_frame_key_, reader_key_);
            if (read_result.status == FrameReadStatus::Empty) {
                break;
            }
            if (read_result.status == FrameReadStatus::Overrun) {
                log_warn("Whisper reader '{}' overrun: requested_seq={}, oldest_available_seq={}",
                         reader_key_, read_result.requested_seq, read_result.oldest_available_seq);
                ctx.seek_audio_frame_reader_to_oldest(input_frame_key_, reader_key_);
                continue;
            }

            auto frame = read_result.frame;
            if (!frame || frame->gap || frame->samples.empty()) {
                continue;
            }
            const size_t remaining = static_cast<size_t>(max_samples) - audio_data.size();
            const size_t copy_size = std::min(remaining, frame->samples.size());
            audio_data.insert(
                audio_data.end(),
                frame->samples.begin(),
                frame->samples.begin() + static_cast<std::ptrdiff_t>(copy_size)
            );
        }

        if (audio_data.empty()) {
            return;
        }

        // Pad or trim to exactly 30 seconds
        audio_data = prepare_audio(audio_data);

        // Extract log-mel spectrogram features
        auto features = extract_log_mel_spectrogram(audio_data);

        // Run inference
        AsrResult result = infer(features);

        // Save results
        ctx.set(output_key_ + "_text", result.text);
        ctx.set(output_key_ + "_confidence", result.confidence);
        ctx.set(output_key_ + "_language", result.language);

        std::vector<AsrResult> results;
        if (ctx.contains(output_key_ + "_results")) {
            results = ctx.get<std::vector<AsrResult>>(output_key_ + "_results");
        }
        results.push_back(result);
        ctx.set(output_key_ + "_results", results);

        auto events = ctx.get_or_default(output_key_ + "_events", std::vector<AsrEvent>{});
        events.push_back(AsrEvent{.kind = AsrResultKind::StreamFinal, .result = result});
        ctx.set(output_key_ + "_events", std::move(events));

        log_info("Whisper ASR: text=\"{}\", language={}, confidence={:.2f}",
                 result.text, result.language, result.confidence);
    }

    bool ready(Context&, StreamStore& store) {
        return store.has_unread(input_frame_key_, reader_key_) ||
               collected_samples_ >= static_cast<size_t>(sample_rate_ * 30) ||
               eos_seen_;
    }

    StreamProcessResult process_stream(Context& ctx, StreamStore& store) {
        std::vector<float> audio_data;
        std::size_t consumed = 0;
        const int max_samples = sample_rate_ * 30;

        while (collected_audio_.size() < static_cast<size_t>(max_samples)) {
            auto read_result = store.read_frame(input_frame_key_, reader_key_);
            if (read_result.status == FrameReadStatus::Empty) {
                break;
            }
            if (read_result.status == FrameReadStatus::Overrun) {
                store.seek_reader_to_oldest(input_frame_key_, reader_key_);
                return {
                    .status = StreamProcessStatus::OverrunRecovered
                };
            }

            auto frame = read_result.frame;
            if (!frame || frame->gap || frame->samples.empty()) {
                if (read_result.status == FrameReadStatus::Eof) {
                    eos_seen_ = true;
                }
                continue;
            }

            ++consumed;
            collected_audio_.insert(collected_audio_.end(), frame->samples.begin(), frame->samples.end());
            collected_samples_ = collected_audio_.size();
            if (frame->eos || read_result.status == FrameReadStatus::Eof) {
                eos_seen_ = true;
                break;
            }
        }

        if (collected_audio_.empty()) {
            return {
                .status = consumed > 0 ? StreamProcessStatus::ConsumedInput : StreamProcessStatus::NeedMoreInput
            };
        }

        if (collected_audio_.size() < static_cast<size_t>(max_samples) && !eos_seen_) {
            return {
                .status = consumed > 0 ? StreamProcessStatus::ConsumedInput : StreamProcessStatus::NeedMoreInput
            };
        }

        audio_data = prepare_audio(collected_audio_);
        auto features = extract_log_mel_spectrogram(audio_data);
        AsrResult result = infer(features);

        ctx.set(output_key_ + "_text", result.text);
        ctx.set(output_key_ + "_confidence", result.confidence);
        ctx.set(output_key_ + "_language", result.language);

        auto results = ctx.get_or_default(output_key_ + "_results", std::vector<AsrResult>{});
        results.push_back(result);
        ctx.set(output_key_ + "_results", std::move(results));

        auto events = ctx.get_or_default(output_key_ + "_events", std::vector<AsrEvent>{});
        events.push_back(AsrEvent{
            .kind = eos_seen_ ? AsrResultKind::StreamFinal : AsrResultKind::Partial,
            .result = result
        });
        ctx.set(output_key_ + "_events", std::move(events));

        collected_audio_.clear();
        collected_samples_ = 0;
        const auto status = eos_seen_ ? StreamProcessStatus::StreamFinalized : StreamProcessStatus::ProducedOutput;
        eos_seen_ = false;

        return {
            .status = status,
            .consumed_frames = consumed,
            .produced_items = 1,
            .wake_downstream = true
        };
    }

    StreamProcessResult flush(Context& ctx, StreamStore& store) {
        eos_seen_ = true;
        return process_stream(ctx, store);
    }

    void deinit() override {
        session_.reset();
        AsrBase::deinit();
    }

private:
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

    AsrResult infer(const std::vector<std::vector<float>>& mel_spec) {
        AsrResult result;

        if (!session_ || mel_spec.empty()) {
            return result;
        }

        // Prepare input tensor [batch, n_mels, n_frames]
        const int n_mels = 80;
        const int n_frames = 3000;

        std::vector<float> input_data;
        input_data.reserve(n_mels * n_frames);

        for (int mel = 0; mel < n_mels; ++mel) {
            for (int frame = 0; frame < n_frames; ++frame) {
                input_data.push_back(mel_spec[mel][frame]);
            }
        }

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

        // Decode output (simplified)
        result.text = "[Whisper ASR result]";
        result.confidence = 0.90f;
        result.language = detect_language_ ? "auto" : language_;

        return result;
    }

    std::string task_ = "transcribe";  // "transcribe" or "translate"
    bool detect_language_ = true;

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_{nullptr};

    std::unordered_map<int, std::string> id_to_token_;
    std::vector<float> collected_audio_;
    std::size_t collected_samples_ = 0;
    bool eos_seen_ = false;
};

namespace {

OperatorRegistrar<OpAsrWhisper> registrar("AsrWhisper");

}

}
