module;

#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

export module yspeech.op.vad.silero;

import std;
import yspeech.context;
import yspeech.error;
import yspeech.op;
import yspeech.log;
import yspeech.types;

namespace yspeech {

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
        try {
            validate_and_load_config(config);
            init_onnx_session();
            reset_state();

            log_info("OpSileroVad initialized: model={}, threshold={}, sample_rate={}",
                     model_path_, threshold_, sample_rate_);
        } catch (const std::exception& e) {
            log_error("OpSileroVad initialization failed: {}", e.what());
            deinit();
            throw;
        }
    }

    void process(Context& ctx) {
        try {
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
            size_t popped = audio_buffer->channels[0]->pop_batch(audio_chunk.data(), WINDOW_SIZE);
            if (popped < WINDOW_SIZE) {
                log_warn("VAD: Expected {} samples but only got {}", WINDOW_SIZE, popped);
                return;
            }

            float probability = infer(audio_chunk);

            update_state(probability, audio_chunk.size());

            ctx.set(output_key_ + "_probability", probability);
            ctx.set(output_key_ + "_is_speech", is_speech_);

            if (segment_finished_) {
                VadSegment segment{
                    .start_ms = static_cast<int64_t>(current_segment_start_ms_),
                    .end_ms = static_cast<int64_t>(current_segment_end_ms_),
                    .confidence = current_segment_confidence_
                };

                std::vector<VadSegment> segments;
                if (ctx.contains(output_key_ + "_segments")) {
                    segments = ctx.get<std::vector<VadSegment>>(output_key_ + "_segments");
                }
                segments.push_back(segment);
                ctx.set(output_key_ + "_segments", segments);

                log_info("VAD segment detected: [{:.0f}ms - {:.0f}ms], confidence={:.2f}",
                         segment.start_ms, segment.end_ms, segment.confidence);

                reset_segment_state();
            }

            log_debug("VAD inference: probability={:.3f}, is_speech={}", probability, is_speech_);
        } catch (const std::exception& e) {
            log_error("VAD processing error: {}", e.what());
            ctx.record_error("OpSileroVad", e.what(), "VAD", ErrorCode::OperatorProcessFailed, ErrorLevel::Error);
        }
    }

    void deinit() {
        session_.reset();
        env_.reset();
        memory_info_ = Ort::MemoryInfo{nullptr};
    }

    bool is_speech() const { return is_speech_; }
    float current_probability() const { return last_probability_; }

private:
    void validate_and_load_config(const nlohmann::json& config) {
        if (!config.contains("model_path")) {
            throw std::invalid_argument("OpSileroVad: 'model_path' is required in config");
        }
        model_path_ = config["model_path"].get<std::string>();

        if (config.contains("threshold")) {
            threshold_ = config["threshold"].get<float>();
            if (threshold_ < 0.0f || threshold_ > 1.0f) {
                throw std::invalid_argument("OpSileroVad: 'threshold' must be between 0.0 and 1.0");
            }
        }

        if (config.contains("sample_rate")) {
            sample_rate_ = config["sample_rate"].get<int>();
            if (sample_rate_ != 8000 && sample_rate_ != 16000) {
                throw std::invalid_argument("OpSileroVad: 'sample_rate' must be 8000 or 16000");
            }
        }

        if (config.contains("input_buffer_key")) {
            input_buffer_key_ = config["input_buffer_key"].get<std::string>();
        }

        if (config.contains("output_key")) {
            output_key_ = config["output_key"].get<std::string>();
        }

        if (config.contains("min_speech_duration_ms")) {
            min_speech_duration_ms_ = config["min_speech_duration_ms"].get<int>();
            if (min_speech_duration_ms_ < 0) {
                throw std::invalid_argument("OpSileroVad: 'min_speech_duration_ms' must be non-negative");
            }
        }

        if (config.contains("min_silence_duration_ms")) {
            min_silence_duration_ms_ = config["min_silence_duration_ms"].get<int>();
            if (min_silence_duration_ms_ < 0) {
                throw std::invalid_argument("OpSileroVad: 'min_silence_duration_ms' must be non-negative");
            }
        }
    }

    void init_onnx_session() {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "yspeech_vad");
        env_ = std::make_unique<Ort::Env>(std::move(env));

        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(2);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = std::make_unique<Ort::Session>(*env_, model_path_.c_str(), session_options);
        memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        discover_model_io();
        validate_model_io();

        log_info("ONNX session created for {}", model_path_);
    }

    void discover_model_io() {
        Ort::AllocatorWithDefaultOptions allocator;

        size_t num_inputs = session_->GetInputCount();
        log_debug("Model has {} inputs:", num_inputs);

        for (size_t i = 0; i < num_inputs; ++i) {
            auto name = session_->GetInputNameAllocated(i, allocator);
            auto type_info = session_->GetInputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();

            std::string name_str(name.get());
            input_names_.push_back(name_str);

            std::string shape_str = shape_to_string(shape);
            log_debug("  Input {}: name='{}', shape=[{}]", i, name_str, shape_str);

            if (name_str == "x" || name_str == "input") {
                audio_input_idx_ = static_cast<int>(i);
            } else if (name_str == "h" || name_str.find("hidden") != std::string::npos) {
                h_input_idx_ = static_cast<int>(i);
                h_state_shape_ = normalize_shape(shape);
            } else if (name_str == "c" || name_str.find("cell") != std::string::npos) {
                c_input_idx_ = static_cast<int>(i);
                c_state_shape_ = normalize_shape(shape);
            } else if (name_str == "sr" || name_str.find("sample") != std::string::npos) {
                sr_input_idx_ = static_cast<int>(i);
            }
        }

        size_t num_outputs = session_->GetOutputCount();
        log_debug("Model has {} outputs:", num_outputs);

        for (size_t i = 0; i < num_outputs; ++i) {
            auto name = session_->GetOutputNameAllocated(i, allocator);
            auto type_info = session_->GetOutputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();

            std::string name_str(name.get());
            output_names_.push_back(name_str);

            std::string shape_str = shape_to_string(shape);
            log_debug("  Output {}: name='{}', shape=[{}]", i, name_str, shape_str);

            if (name_str == "prob" || name_str == "output" || name_str.find("prob") != std::string::npos) {
                prob_output_idx_ = static_cast<int>(i);
                prob_output_name_ = name_str;
            } else if (name_str.find("h") != std::string::npos || name_str.find("hidden") != std::string::npos) {
                h_output_idx_ = static_cast<int>(i);
                h_output_name_ = name_str;
                h_state_shape_ = normalize_shape(shape);
            } else if (name_str.find("c") != std::string::npos || name_str.find("cell") != std::string::npos) {
                c_output_idx_ = static_cast<int>(i);
                c_output_name_ = name_str;
                c_state_shape_ = normalize_shape(shape);
            }
        }
    }

    std::string shape_to_string(const std::vector<int64_t>& shape) {
        std::string result;
        for (size_t i = 0; i < shape.size(); ++i) {
            if (shape[i] < 0) {
                result += "dyn";
            } else {
                result += std::to_string(shape[i]);
            }
            if (i < shape.size() - 1) result += ",";
        }
        return result;
    }

    std::vector<int64_t> normalize_shape(const std::vector<int64_t>& shape) {
        std::vector<int64_t> result;
        for (auto dim : shape) {
            result.push_back(dim < 0 ? 1 : dim);
        }
        return result;
    }

    size_t compute_shape_size(const std::vector<int64_t>& shape) {
        size_t size = 1;
        for (auto dim : shape) {
            if (dim > 0) size *= static_cast<size_t>(dim);
        }
        return size;
    }

    void validate_model_io() {
        if (audio_input_idx_ == -1) {
            throw std::runtime_error("OpSileroVad: Could not find audio input (expected 'x' or 'input')");
        }
        if (prob_output_idx_ == -1) {
            throw std::runtime_error("OpSileroVad: Could not find probability output (expected 'prob' or 'output')");
        }

        if (h_input_idx_ >= 0 && h_state_shape_.empty()) {
            throw std::runtime_error("OpSileroVad: h state shape not determined");
        }
        if (c_input_idx_ >= 0 && c_state_shape_.empty()) {
            throw std::runtime_error("OpSileroVad: c state shape not determined");
        }

        log_info("Model I/O mapping: audio_input_idx={}, h_idx={}, c_idx={}, sr_idx={}, prob_idx={}, h_out_idx={}, c_out_idx={}",
                 audio_input_idx_, h_input_idx_, c_input_idx_, sr_input_idx_,
                 prob_output_idx_, h_output_idx_, c_output_idx_);
    }

    void reset_state() {
        if (!h_state_shape_.empty()) {
            h_state_.assign(compute_shape_size(h_state_shape_), 0.0f);
        }
        if (!c_state_shape_.empty()) {
            c_state_.assign(compute_shape_size(c_state_shape_), 0.0f);
        }
        last_probability_ = 0.0f;
        is_speech_ = false;
        speech_start_sample_ = 0;
        silence_duration_samples_ = 0;
        speech_duration_samples_ = 0;
        total_processed_samples_ = 0;
        reset_segment_state();
    }

    void reset_segment_state() {
        current_segment_start_sample_ = 0;
        current_segment_end_sample_ = 0;
        current_segment_confidence_ = 0.0f;
        segment_sample_count_ = 0;
        segment_finished_ = false;
    }

    float infer(const std::vector<float>& audio_chunk) {
        std::vector<int64_t> input_shape = {1, static_cast<int64_t>(audio_chunk.size())};
        std::vector<int64_t> sr_shape = {1};

        audio_input_buffer_ = audio_chunk;

        std::vector<Ort::Value> inputs;
        std::vector<const char*> input_names;

        Ort::Value audio_tensor = Ort::Value::CreateTensor<float>(
            memory_info_, audio_input_buffer_.data(), audio_input_buffer_.size(),
            input_shape.data(), input_shape.size());
        inputs.push_back(std::move(audio_tensor));
        input_names.push_back(input_names_[audio_input_idx_].c_str());

        if (h_input_idx_ >= 0 && !h_state_.empty()) {
            Ort::Value h_tensor = Ort::Value::CreateTensor<float>(
                memory_info_, h_state_.data(), h_state_.size(),
                h_state_shape_.data(), h_state_shape_.size());
            inputs.push_back(std::move(h_tensor));
            input_names.push_back(input_names_[h_input_idx_].c_str());
        }

        if (c_input_idx_ >= 0 && !c_state_.empty()) {
            Ort::Value c_tensor = Ort::Value::CreateTensor<float>(
                memory_info_, c_state_.data(), c_state_.size(),
                c_state_shape_.data(), c_state_shape_.size());
            inputs.push_back(std::move(c_tensor));
            input_names.push_back(input_names_[c_input_idx_].c_str());
        }

        if (sr_input_idx_ >= 0) {
            sr_value_ = static_cast<int64_t>(sample_rate_);
            Ort::Value sr_tensor = Ort::Value::CreateTensor<int64_t>(
                memory_info_, &sr_value_, 1, sr_shape.data(), sr_shape.size());
            inputs.push_back(std::move(sr_tensor));
            input_names.push_back(input_names_[sr_input_idx_].c_str());
        }

        std::vector<const char*> output_names;
        std::vector<size_t> output_name_indices;

        auto add_output = [&](int idx, const std::string& name) {
            if (idx >= 0) {
                output_names.push_back(name.c_str());
                output_name_indices.push_back(static_cast<size_t>(idx));
            }
        };

        add_output(prob_output_idx_, prob_output_name_);
        add_output(h_output_idx_, h_output_name_);
        add_output(c_output_idx_, c_output_name_);

        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names.data(),
            inputs.data(),
            inputs.size(),
            output_names.data(),
            output_names.size()
        );

        float probability = 0.0f;

        for (size_t i = 0; i < outputs.size() && i < output_name_indices.size(); ++i) {
            int original_idx = static_cast<int>(output_name_indices[i]);

            if (original_idx == prob_output_idx_) {
                float* output_data = outputs[i].GetTensorMutableData<float>();
                if (output_data) {
                    auto shape = outputs[i].GetTensorTypeAndShapeInfo().GetShape();
                    if (!shape.empty() && shape[0] > 0) {
                        probability = output_data[0];
                    }
                }
            } else if (original_idx == h_output_idx_) {
                float* new_h = outputs[i].GetTensorMutableData<float>();
                if (new_h && !h_state_.empty()) {
                    auto shape = outputs[i].GetTensorTypeAndShapeInfo().GetShape();
                    size_t expected_size = compute_shape_size(shape);
                    if (expected_size == h_state_.size()) {
                        std::copy(new_h, new_h + h_state_.size(), h_state_.begin());
                    } else {
                        log_warn("VAD: h state size mismatch, expected {} but got {}", h_state_.size(), expected_size);
                    }
                }
            } else if (original_idx == c_output_idx_) {
                float* new_c = outputs[i].GetTensorMutableData<float>();
                if (new_c && !c_state_.empty()) {
                    auto shape = outputs[i].GetTensorTypeAndShapeInfo().GetShape();
                    size_t expected_size = compute_shape_size(shape);
                    if (expected_size == c_state_.size()) {
                        std::copy(new_c, new_c + c_state_.size(), c_state_.begin());
                    } else {
                        log_warn("VAD: c state size mismatch, expected {} but got {}", c_state_.size(), expected_size);
                    }
                }
            }
        }

        return probability;
    }

    void update_state(float probability, size_t samples_processed) {
        last_probability_ = probability;
        total_processed_samples_ += samples_processed;

        bool speech_detected = probability >= threshold_;

        if (speech_detected) {
            if (!is_speech_) {
                speech_start_sample_ = total_processed_samples_ - samples_processed;
                speech_duration_samples_ = samples_processed;
                silence_duration_samples_ = 0;
                current_segment_start_sample_ = speech_start_sample_;
                current_segment_confidence_ = probability;
                segment_sample_count_ = 1;
            } else {
                speech_duration_samples_ += samples_processed;
                silence_duration_samples_ = 0;
                current_segment_confidence_ = (current_segment_confidence_ * segment_sample_count_ + probability)
                                              / (segment_sample_count_ + 1);
                segment_sample_count_++;
            }
            is_speech_ = true;
        } else {
            if (is_speech_) {
                silence_duration_samples_ += samples_processed;

                int min_silence_samples = (min_silence_duration_ms_ * sample_rate_) / 1000;
                int min_speech_samples = (min_speech_duration_ms_ * sample_rate_) / 1000;

                if (silence_duration_samples_ >= static_cast<size_t>(min_silence_samples)) {
                    if (speech_duration_samples_ >= static_cast<size_t>(min_speech_samples)) {
                        current_segment_end_sample_ = total_processed_samples_ - silence_duration_samples_;
                        segment_finished_ = true;
                    }
                    is_speech_ = false;
                    speech_duration_samples_ = 0;
                }
            }
        }

        current_segment_start_ms_ = (static_cast<float>(current_segment_start_sample_) / sample_rate_) * 1000.0f;
        current_segment_end_ms_ = (static_cast<float>(current_segment_end_sample_) / sample_rate_) * 1000.0f;
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

    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;

    std::string prob_output_name_;
    std::string h_output_name_;
    std::string c_output_name_;

    int audio_input_idx_ = -1;
    int h_input_idx_ = -1;
    int c_input_idx_ = -1;
    int sr_input_idx_ = -1;
    int prob_output_idx_ = -1;
    int h_output_idx_ = -1;
    int c_output_idx_ = -1;

    std::vector<int64_t> h_state_shape_;
    std::vector<int64_t> c_state_shape_;
    std::vector<float> h_state_;
    std::vector<float> c_state_;
    std::vector<float> audio_input_buffer_;
    int64_t sr_value_ = SAMPLE_RATE;

    float last_probability_ = 0.0f;
    bool is_speech_ = false;

    size_t speech_start_sample_ = 0;
    size_t silence_duration_samples_ = 0;
    size_t speech_duration_samples_ = 0;
    size_t total_processed_samples_ = 0;

    size_t current_segment_start_sample_ = 0;
    size_t current_segment_end_sample_ = 0;
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
