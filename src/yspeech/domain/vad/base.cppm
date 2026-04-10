module;

#include <nlohmann/json.hpp>

export module yspeech.domain.vad.base;

import std;
import yspeech.types;

namespace yspeech {

export struct SileroVadChunkResult {
    float probability = 0.0f;
    bool is_speech = false;
    std::int64_t current_start_ms = 0;
    std::int64_t current_end_ms = 0;
    std::vector<VadSegment> finished_segments;
};

export class VadCoreIface {
public:
    virtual ~VadCoreIface() = default;
    virtual void init(const nlohmann::json& config) = 0;
    virtual auto process_samples(std::span<const float> samples, bool eos = false) -> SileroVadChunkResult = 0;
    virtual void bind_stats(ProcessingStats* stats) {
        (void)stats;
    }
    virtual void deinit() = 0;
    virtual auto current_probability() const -> float = 0;
};

export class VadCoreFactory {
public:
    using CreatorFunc = std::function<std::unique_ptr<VadCoreIface>()>;

    static VadCoreFactory& get_instance() {
        static VadCoreFactory instance;
        return instance;
    }

    void register_core(const std::string& name, CreatorFunc creator) {
        if (registry_.contains(name)) {
            throw std::runtime_error(std::format("VAD core type already registered: {}", name));
        }
        registry_[name] = std::move(creator);
    }

    auto create_core(const std::string& name) -> std::unique_ptr<VadCoreIface> {
        if (!registry_.contains(name)) {
            throw std::runtime_error(std::format("Unknown VAD core type: {}", name));
        }
        return registry_[name]();
    }

private:
    VadCoreFactory() = default;
    std::unordered_map<std::string, CreatorFunc> registry_;
};

export template<typename T>
struct VadCoreRegistrar {
    explicit VadCoreRegistrar(const std::string& name) {
        VadCoreFactory::get_instance().register_core(name, []() -> std::unique_ptr<VadCoreIface> {
            return std::make_unique<T>();
        });
    }
};

}
