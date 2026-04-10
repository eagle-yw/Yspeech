module;

#include <nlohmann/json.hpp>

export module yspeech.domain.asr.base;

import std;
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
        if (config.contains("__core_id")) {
            core_id_ = config["__core_id"].get<std::string>();
        } else {
            core_id_ = output_key_;
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

    virtual void deinit() {
        log_info("ASR Base deinitialized");
    }

protected:
    std::string model_path_;
    std::string tokens_path_;
    std::string input_frame_key_ = "audio_frames";
    std::string reader_key_ = "asr_reader";
    std::string output_key_ = "asr";
    std::string core_id_ = "asr";
    int sample_rate_ = 16000;
    std::string language_ = "zh";
    bool use_gpu_ = false;
    bool use_coreml_ = false;
    bool coreml_ane_only_ = false;
    std::uint32_t coreml_flags_ = 0;
    int num_threads_ = 4;
};

export using FeatureFrames = std::vector<std::vector<float>>;
export using FeatureChunk = std::shared_ptr<const FeatureFrames>;
export using FeatureChunkList = std::vector<FeatureChunk>;
export using FeatureChunkListPtr = std::shared_ptr<const FeatureChunkList>;

export struct FeatureSequenceView {
    FeatureChunkListPtr chunks;
    int feature_count = 0;

    [[nodiscard]] auto empty() const -> bool {
        return feature_count <= 0 || !chunks || chunks->empty();
    }

    [[nodiscard]] auto feature_dim() const -> int {
        if (!chunks) {
            return 0;
        }
        for (const auto& chunk : *chunks) {
            if (chunk && !chunk->empty()) {
                return static_cast<int>(chunk->front().size());
            }
        }
        return 0;
    }

    static auto from_frames(const FeatureFrames& frames) -> FeatureSequenceView {
        FeatureSequenceView view;
        view.feature_count = static_cast<int>(frames.size());
        if (!frames.empty()) {
            auto chunks = std::make_shared<FeatureChunkList>();
            chunks->push_back(std::make_shared<FeatureFrames>(frames));
            view.chunks = std::move(chunks);
        }
        return view;
    }

    static auto from_chunk_list(FeatureChunkListPtr chunks, int total_feature_count) -> FeatureSequenceView {
        FeatureSequenceView view;
        view.feature_count = total_feature_count;
        view.chunks = std::move(chunks);
        return view;
    }

    static auto from_chunk_copy(const FeatureChunkList& chunks, int total_feature_count) -> FeatureSequenceView {
        FeatureSequenceView view;
        view.feature_count = total_feature_count;
        view.chunks = std::make_shared<FeatureChunkList>(chunks);
        return view;
    }
};

export class AsrCoreIface {
public:
    virtual ~AsrCoreIface() = default;
    virtual void init(const nlohmann::json& config) = 0;
    virtual auto infer(const FeatureSequenceView& features) -> AsrResult = 0;
    virtual void bind_stats(ProcessingStats* stats) {
        (void)stats;
    }
    virtual auto supports_incremental() const -> bool { return false; }
    virtual void accept_features(const std::string& stream_id, const FeatureSequenceView& delta_features) {
        (void)stream_id;
        (void)delta_features;
    }
    virtual auto decode_partial(const std::string& stream_id, const FeatureSequenceView& full_context) -> AsrResult {
        (void)stream_id;
        return infer(full_context);
    }
    virtual auto decode_final(const std::string& stream_id, const FeatureSequenceView& full_context) -> AsrResult {
        (void)stream_id;
        return infer(full_context);
    }
    virtual void reset_stream(const std::string& stream_id) {
        (void)stream_id;
    }
    virtual void deinit() = 0;
};

export class AsrCoreFactory {
public:
    using CreatorFunc = std::function<std::unique_ptr<AsrCoreIface>()>;

    static AsrCoreFactory& get_instance() {
        static AsrCoreFactory instance;
        return instance;
    }

    void register_core(const std::string& name, CreatorFunc creator) {
        if (registry_.contains(name)) {
            throw std::runtime_error(std::format("ASR core type already registered: {}", name));
        }
        registry_[name] = std::move(creator);
    }

    auto create_core(const std::string& name) -> std::unique_ptr<AsrCoreIface> {
        if (!registry_.contains(name)) {
            throw std::runtime_error(std::format("Unknown ASR core type: {}", name));
        }
        return registry_[name]();
    }

    bool has_core(const std::string& name) const {
        return registry_.contains(name);
    }

private:
    AsrCoreFactory() = default;
    std::unordered_map<std::string, CreatorFunc> registry_;
};

export template<typename T>
struct AsrCoreRegistrar {
    AsrCoreRegistrar(const std::string& name) {
        AsrCoreFactory::get_instance().register_core(name, []() -> std::unique_ptr<AsrCoreIface> {
            return std::make_unique<T>();
        });
    }
};

}
