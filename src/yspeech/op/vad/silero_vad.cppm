module;

#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

export module yspeech.op.silero_vad;

import std;
import yspeech.context;
import yspeech.op;
import yspeech.log;
import yspeech.types;

namespace yspeech {

export struct VadSegment {
    float start_time_ms;
    float end_time_ms;
    float confidence;
};

export class OpSileroVad {
public:
    static constexpr int WINDOW_SIZE = 512;
    static constexpr int SAMPLE_RATE = 16000;
    static constexpr float DEFAULT_THRESHOLD = 0.5f;
    static constexpr int MIN_SPEECH_DURATION_MS = 250;
    static constexpr int MIN_SILENCE_DURATION_MS = 100;

    OpSileroVad() = default;

    OpSileroVad(const OpSileroVad&) = delete;
    OpSileroVad& operator=(const OpSileroVad&) = delete;
    OpSileroVad(OpSileroVad&&) noexcept = default;
    OpSileroVad& operator=(OpSileroVad&&) noexcept = default;

    void init(const nlohmann::json& config) {
        if (config.contains("model_path")) {
            model_path_ = config["model_path"].get<std::string>();
        }
        if (config.contains("threshold")) {
            threshold_ = config["threshold"].get<float>();
        }
        if (config.contains("sample_rate")) {
            sample_rate_ = config["sample_rate"].get<int>();
        }
        if (config.contains("input_buffer_key")) {
            input_buffer_key_ = config["input_buffer_key"].get<std::string>();
        }
        if (config.contains("output_key")) {
            output_key_ = config["output_key"].get<std::string>();
        }
        if (config.contains("min_speech_duration_ms")) {
            min_speech_duration_ms_ = config["min_speech_duration_ms"].get<int>();
        }
        if (config.contains("min_silence_duration_ms")) {
            min_silence_duration_ms_ = config["min_silence_duration_ms"].get<int>();
        }

        init_onnx_session();
        reset_state();

        log_info("OpSileroVad initialized: model={}, threshold={}, sample_rate={}",
                 model_path_, threshold_, sample_rate_);
    }

    void process(Context& ctx) {
        auto audio_buffer = ctx.get_audio_buffer(input_buffer_key_);
        if (!audio_buffer || audio_buffer->channels.empty()) {
            log_debug("No audio buffer available");
            return;
        }

        size_t available = audio_buffer->channels[0]->size();
        if (available < WINDOW_SIZE) {
            return;
        }

        std::vector<float> audio_chunk(WINDOW_SIZE);
        for (size_t i = 0; i < WINDOW_SIZE; ++i) {
            if (!audio_buffer->channels[0]->pop(audio_chunk[i])) {
                break;
            }
        }

        float probability = infer(audio_chunk);

        update_state(probability, audio_chunk.size());

        ctx.set(output_key_ + "_probability", probability);
        ctx.set(output_key_ + "_is_speech", is_speech_);

        if (segment_finished_) {
            VadSegment segment{
                .start_time_ms = current_segment_start_ms_,
                .end_time_ms = current_segment_end_ms_,
                .confidence = current_segment_confidence_
            };

            std::vector<VadSegment> segments;
            if (ctx.contains(output_key_ + "_segments")) {
                segments = ctx.get<std::vector<VadSegment>>(output_key_ + "_segments");
            }
            segments.push_back(segment);
            ctx.set(output_key_ + "_segments", segments);

            log_info("VAD segment detected: [{:.0f}ms - {:.0f}ms], confidence={:.2f}",
                     segment.start_time_ms, segment.end_time_ms, segment.confidence);

            reset_segment_state();
        }

        log_debug("VAD inference: probability={:.3f}, is_speech={}", probability, is_speech_);
    }

    void deinit() {
        session_.reset();
        log_info("OpSileroVad deinitialized");
    }

    bool is_speech() const { return is_speech_; }
    float current_probability() const { return last_probability_; }

private:
    void init_onnx_session() {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "yspeech_vad");
        env_ = std::make_unique<Ort::Env>(std::move(env));

        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetInterOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = std::make_unique<Ort::Session>(*env_, model_path_.c_str(), session_options);
        memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // Silero VAD model has 3 inputs: x (audio), h (state), c (state+c in this model)
        // Use hardcoded names based on the model inspection
        input_name_ = "x";
        h_name_ = "h";
        c_name_ = "c";

        output_name_ = "prob";
        h_out_name_ = "new_h";
        c_out_name_ = "new_c";

        log_info("ONNX Model I/O: input={}, h={}, c={}, sr={}, output={}, h_out={}, c_out={}",
                 input_name_, h_name_, c_name_, sr_name_, output_name_, h_out_name_, c_out_name_);
    }

    void reset_state() {
        // Silero VAD uses LSTM with h and c states, each with shape [2, 1, 64] (num_layers, batch, hidden_size)
        h_state_.assign(2 * 1 * 64, 0.0f);
        c_state_.assign(2 * 1 * 64, 0.0f);
        last_probability_ = 0.0f;
        is_speech_ = false;
        speech_start_ms_ = 0.0f;
        silence_duration_ms_ = 0.0f;
        speech_duration_ms_ = 0.0f;
        total_processed_ms_ = 0.0f;
        reset_segment_state();
    }

    void reset_segment_state() {
        current_segment_start_ms_ = 0.0f;
        current_segment_end_ms_ = 0.0f;
        current_segment_confidence_ = 0.0f;
        segment_sample_count_ = 0;
        segment_finished_ = false;
    }

    float infer(const std::vector<float>& audio_chunk) {
        // Input shapes for Silero VAD
        std::vector<int64_t> input_shape = {1, static_cast<int64_t>(audio_chunk.size())};  // [batch, samples]
        std::vector<int64_t> state_shape = {2, 1, 64};  // [num_layers, batch, hidden_size]

        // Create input tensors
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info_, const_cast<float*>(audio_chunk.data()), audio_chunk.size(), input_shape.data(), input_shape.size());

        Ort::Value h_tensor = Ort::Value::CreateTensor<float>(
            memory_info_, h_state_.data(), h_state_.size(), state_shape.data(), state_shape.size());

        Ort::Value c_tensor = Ort::Value::CreateTensor<float>(
            memory_info_, c_state_.data(), c_state_.size(), state_shape.data(), state_shape.size());

        // Prepare inputs and outputs
        std::vector<Ort::Value> inputs;
        inputs.push_back(std::move(input_tensor));
        inputs.push_back(std::move(h_tensor));
        inputs.push_back(std::move(c_tensor));

        std::vector<const char*> input_names = {input_name_.c_str(), h_name_.c_str(), c_name_.c_str()};
        std::vector<const char*> output_names = {output_name_.c_str(), h_out_name_.c_str(), c_out_name_.c_str()};

        // Run inference
        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names.data(),
            inputs.data(),
            inputs.size(),
            output_names.data(),
            output_names.size()
        );

        // Get output probability
        float* output_data = outputs[0].GetTensorMutableData<float>();
        float probability = output_data[0];

        // Update states for next inference
        float* new_h = outputs[1].GetTensorMutableData<float>();
        float* new_c = outputs[2].GetTensorMutableData<float>();
        std::copy(new_h, new_h + h_state_.size(), h_state_.begin());
        std::copy(new_c, new_c + c_state_.size(), c_state_.begin());

        return probability;
    }

    void update_state(float probability, size_t samples_processed) {
        last_probability_ = probability;

        float duration_ms = (static_cast<float>(samples_processed) / sample_rate_) * 1000.0f;
        total_processed_ms_ += duration_ms;

        bool speech_detected = probability >= threshold_;

        if (speech_detected) {
            if (!is_speech_) {
                speech_start_ms_ = total_processed_ms_ - duration_ms;
                speech_duration_ms_ = duration_ms;
                silence_duration_ms_ = 0.0f;
                current_segment_start_ms_ = speech_start_ms_;
                current_segment_confidence_ = probability;
                segment_sample_count_ = 1;
            } else {
                speech_duration_ms_ += duration_ms;
                silence_duration_ms_ = 0.0f;
                current_segment_confidence_ = (current_segment_confidence_ * segment_sample_count_ + probability) / (segment_sample_count_ + 1);
                segment_sample_count_++;
            }
            is_speech_ = true;
        } else {
            if (is_speech_) {
                silence_duration_ms_ += duration_ms;

                if (silence_duration_ms_ >= min_silence_duration_ms_) {
                    if (speech_duration_ms_ >= min_speech_duration_ms_) {
                        current_segment_end_ms_ = total_processed_ms_ - silence_duration_ms_;
                        segment_finished_ = true;
                    }
                    is_speech_ = false;
                    speech_duration_ms_ = 0.0f;
                }
            }
        }
    }

    std::string model_path_ = "silero_vad.onnx";
    float threshold_ = DEFAULT_THRESHOLD;
    int sample_rate_ = SAMPLE_RATE;
    std::string input_buffer_key_ = "audio_planar";
    std::string output_key_ = "vad";
    int min_speech_duration_ms_ = MIN_SPEECH_DURATION_MS;
    int min_silence_duration_ms_ = MIN_SILENCE_DURATION_MS;

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_{nullptr};

    std::string input_name_;
    std::string h_name_;
    std::string c_name_;
    std::string sr_name_;
    std::string output_name_;
    std::string h_out_name_;
    std::string c_out_name_;

    std::vector<float> h_state_;
    std::vector<float> c_state_;
    float last_probability_ = 0.0f;
    bool is_speech_ = false;

    float speech_start_ms_ = 0.0f;
    float silence_duration_ms_ = 0.0f;
    float speech_duration_ms_ = 0.0f;
    float total_processed_ms_ = 0.0f;

    float current_segment_start_ms_ = 0.0f;
    float current_segment_end_ms_ = 0.0f;
    float current_segment_confidence_ = 0.0f;
    int segment_sample_count_ = 0;
    bool segment_finished_ = false;
};

namespace {

OperatorRegistrar<OpSileroVad> registrar("SileroVad");

}

}
