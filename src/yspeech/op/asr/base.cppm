module;

#include <nlohmann/json.hpp>

export module yspeech.op.asr.base;

import std;
import yspeech.context;
import yspeech.op;
import yspeech.log;
import yspeech.stream_store;
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
        if (config.contains("__op_id")) {
            operator_id_ = config["__op_id"].get<std::string>();
        } else {
            operator_id_ = output_key_;
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
        if (config.contains("execution_provider")) {
            auto ep = config["execution_provider"].get<std::string>();
            std::ranges::transform(ep, ep.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (ep == "coreml") {
                use_coreml_ = true;
            }
        }
        if (config.contains("use_coreml")) {
            use_coreml_ = config["use_coreml"].get<bool>();
        }
        if (config.contains("coreml_ane_only")) {
            coreml_ane_only_ = config["coreml_ane_only"].get<bool>();
        }
        if (config.contains("coreml_flags")) {
            coreml_flags_ = config["coreml_flags"].get<std::uint32_t>();
        }
        if (config.contains("num_threads")) {
            num_threads_ = config["num_threads"].get<int>();
        }

        log_info("ASR Base initialized: model_path={}, sample_rate={}, language={}",
                 model_path_, sample_rate_, language_);
    }

    virtual StreamProcessResult process_stream(Context& ctx, StreamStore& store) = 0;

    virtual StreamProcessResult flush(Context&, StreamStore&) {
        return {};
    }

    virtual void deinit() {
        log_info("ASR Base deinitialized");
    }

protected:
    std::string model_path_;
    std::string tokens_path_;
    std::string input_frame_key_ = "audio_frames";
    std::string reader_key_ = "asr_reader";
    std::string output_key_ = "asr";
    std::string operator_id_ = "asr";
    int sample_rate_ = 16000;
    std::string language_ = "zh";
    bool use_gpu_ = false;
    bool use_coreml_ = false;
    bool coreml_ane_only_ = false;
    std::uint32_t coreml_flags_ = 0;
    int num_threads_ = 4;
};

}
