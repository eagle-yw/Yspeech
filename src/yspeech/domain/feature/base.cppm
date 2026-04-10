module;

#include <nlohmann/json.hpp>

export module yspeech.domain.feature.base;

import std;
import yspeech.types;

namespace yspeech {

export struct KaldiFbankOutput {
    std::vector<std::vector<float>> delta_features;
    std::vector<std::vector<float>> features;
    int delta_num_frames = 0;
    int num_frames = 0;
    int accumulated_num_frames = 0;
    int num_bins = 0;
    std::uint64_t version = 0;
};

export class FeatureCoreIface {
public:
    virtual ~FeatureCoreIface() = default;
    virtual void init(const nlohmann::json& config) = 0;
    virtual auto process_samples(std::span<const float> samples, bool eos = false)
        -> std::optional<KaldiFbankOutput> = 0;
    virtual void bind_stats(ProcessingStats* stats) {
        (void)stats;
    }
    virtual void deinit() = 0;
};

export class FeatureCoreFactory {
public:
    using CreatorFunc = std::function<std::unique_ptr<FeatureCoreIface>()>;

    static FeatureCoreFactory& get_instance() {
        static FeatureCoreFactory instance;
        return instance;
    }

    void register_core(const std::string& name, CreatorFunc creator) {
        if (registry_.contains(name)) {
            throw std::runtime_error(std::format("Feature core type already registered: {}", name));
        }
        registry_[name] = std::move(creator);
    }

    auto create_core(const std::string& name) -> std::unique_ptr<FeatureCoreIface> {
        if (!registry_.contains(name)) {
            throw std::runtime_error(std::format("Unknown Feature core type: {}", name));
        }
        return registry_[name]();
    }

private:
    FeatureCoreFactory() = default;
    std::unordered_map<std::string, CreatorFunc> registry_;
};

export template<typename T>
struct FeatureCoreRegistrar {
    explicit FeatureCoreRegistrar(const std::string& name) {
        FeatureCoreFactory::get_instance().register_core(name, []() -> std::unique_ptr<FeatureCoreIface> {
            return std::make_unique<T>();
        });
    }
};

}
