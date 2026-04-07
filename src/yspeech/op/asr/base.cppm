module;

#include <nlohmann/json.hpp>

export module yspeech.op.asr.base;

import std;
import yspeech.context;
import yspeech.op;
import yspeech.log;
import yspeech.types;

namespace yspeech {

export class AsrBase {
public:
    virtual ~AsrBase() = default;

    virtual void init(const nlohmann::json& config) {
        if (config.contains("model_path")) {
            model_path_ = config["model_path"].get<std::string>();
        }
        if (config.contains("tokens_path")) {
            tokens_path_ = config["tokens_path"].get<std::string>();
        }
        if (config.contains("input_frame_key")) {
            input_frame_key_ = config["input_frame_key"].get<std::string>();
        }
        if (config.contains("output_key")) {
            output_key_ = config["output_key"].get<std::string>();
        }
        reader_key_ = output_key_ + "_reader";
        if (config.contains("reader_key")) {
            reader_key_ = config["reader_key"].get<std::string>();
        }
        if (config.contains("sample_rate")) {
            sample_rate_ = config["sample_rate"].get<int>();
        }
        if (config.contains("language")) {
            language_ = config["language"].get<std::string>();
        }
        if (config.contains("use_gpu")) {
            use_gpu_ = config["use_gpu"].get<bool>();
        }
        if (config.contains("num_threads")) {
            num_threads_ = config["num_threads"].get<int>();
        }

        log_info("ASR Base initialized: model_path={}, sample_rate={}, language={}",
                 model_path_, sample_rate_, language_);
    }

    virtual void process_batch(Context& ctx) = 0;

    virtual void deinit() {
        log_info("ASR Base deinitialized");
    }

    virtual bool is_streaming() const { return false; }

protected:
    std::string model_path_;
    std::string tokens_path_;
    std::string input_frame_key_ = "audio_frames";
    std::string reader_key_ = "asr_reader";
    std::string output_key_ = "asr";
    int sample_rate_ = 16000;
    std::string language_ = "zh";
    bool use_gpu_ = false;
    int num_threads_ = 4;
};

}
