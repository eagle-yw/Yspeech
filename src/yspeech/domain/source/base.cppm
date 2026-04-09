module;

#include <nlohmann/json.hpp>

export module yspeech.domain.source.base;

import std;
import yspeech.runtime.runtime_context;
import yspeech.runtime.token;

namespace yspeech {

export class SourceCoreIface {
public:
    virtual ~SourceCoreIface() = default;
    virtual void init(const nlohmann::json& config) = 0;
    virtual void process(PipelineToken& token, RuntimeContext& runtime) = 0;
    virtual void deinit() = 0;
};

export class SourceCoreFactory {
public:
    using CreatorFunc = std::function<std::unique_ptr<SourceCoreIface>()>;

    static SourceCoreFactory& get_instance() {
        static SourceCoreFactory instance;
        return instance;
    }

    void register_core(const std::string& name, CreatorFunc creator) {
        if (registry_.contains(name)) {
            throw std::runtime_error(std::format("Source core type already registered: {}", name));
        }
        registry_[name] = std::move(creator);
    }

    auto create_core(const std::string& name) -> std::unique_ptr<SourceCoreIface> {
        if (!registry_.contains(name)) {
            throw std::runtime_error(std::format("Unknown source core type: {}", name));
        }
        return registry_[name]();
    }

private:
    SourceCoreFactory() = default;
    std::unordered_map<std::string, CreatorFunc> registry_;
};

export template <typename T>
struct SourceCoreRegistrar {
    explicit SourceCoreRegistrar(const std::string& name) {
        SourceCoreFactory::get_instance().register_core(name, []() -> std::unique_ptr<SourceCoreIface> {
            return std::make_unique<T>();
        });
    }
};

}
