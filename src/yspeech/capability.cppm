module;

#include <nlohmann/json.hpp>

export module yspeech.capability;

import std;
import yspeech.runtime.runtime_context;

namespace yspeech {

using json = nlohmann::json;

export enum class CapabilityPhase {
    Pre,
    Post
};

export template <typename T>
concept Capability = requires(T t, RuntimeContext& runtime, const json& config) {
    { t.init(config) } -> std::same_as<void>;
    { t.apply(runtime) } -> std::same_as<void>;
    { t.phase() } -> std::same_as<CapabilityPhase>;
};

export class CapabilityIface {
public:
    template<Capability T>
    CapabilityIface(T&& cap, std::string name = "")
        : name_(std::move(name))
        , phase_(cap.phase())
        , self_(std::make_unique<Model<std::remove_cvref_t<T>>>(std::forward<T>(cap))) {}

    CapabilityIface(const CapabilityIface&) = delete;
    CapabilityIface& operator=(const CapabilityIface&) = delete;
    CapabilityIface(CapabilityIface&&) noexcept = default;
    CapabilityIface& operator=(CapabilityIface&&) noexcept = default;

    auto init(const json& config) -> void {
        self_->init(config);
    }

    auto apply(RuntimeContext& runtime) -> void {
        self_->apply(runtime);
    }

    auto name() const -> const std::string& {
        return name_;
    }

    auto phase() const -> CapabilityPhase {
        return phase_;
    }

    auto type() const -> const std::type_info& {
        return self_->type();
    }

    struct Concept {
        virtual ~Concept() = default;
        virtual auto init(const json& config) -> void = 0;
        virtual auto apply(RuntimeContext& runtime) -> void = 0;
        virtual auto type() const -> const std::type_info& = 0;
    };

    template<Capability T>
    struct Model: Concept {
        Model(const T& cap): cap_(cap) {}
        Model(T&& cap): cap_(std::move(cap)) {}

        auto init(const json& config) -> void override {
            cap_.init(config);
        }

        auto apply(RuntimeContext& runtime) -> void override {
            cap_.apply(runtime);
        }

        auto type() const -> const std::type_info& override {
            return typeid(T);
        }

        T cap_;
    };

private:
    std::string name_;
    CapabilityPhase phase_;
    std::unique_ptr<Concept> self_;
};

export class CapabilityFactory {
public:
    using CreatorFunc = std::function<CapabilityIface(const json&)>;

    static CapabilityFactory& get_instance() {
        static CapabilityFactory instance;
        return instance;
    }

    void register_capability(const std::string& name, CreatorFunc creator) {
        if (registry_.contains(name)) {
            throw std::runtime_error(std::format("Capability type already registered: {}", name));
        }
        registry_[name] = std::move(creator);
    }

    CapabilityIface create_capability(const std::string& name, const json& config = {}) {
        if (!registry_.contains(name)) {
            throw std::runtime_error(std::format("Unknown capability type: {}", name));
        }
        auto cap = registry_[name](config);
        cap.init(config);
        return cap;
    }

    bool has_capability(const std::string& name) const {
        return registry_.contains(name);
    }

private:
    CapabilityFactory() = default;
    std::unordered_map<std::string, CreatorFunc> registry_;
};

export template<typename T>
struct CapabilityRegistrar {
    CapabilityRegistrar(std::string name = "") {
        if (name.empty()) {
            name = typeid(T).name();
        }
        CapabilityFactory::get_instance().register_capability(name, [name](const json& config) -> CapabilityIface {
            return CapabilityIface(T(config), name);
        });
    }
};

namespace detail {

template<typename T>
inline std::string extract_type_name() {
#if defined(__clang__) || defined(__GNUC__)
    std::string name = __PRETTY_FUNCTION__;
    auto start = name.find("T = ") + 4;
    auto end = name.find(';', start);
    if (end == std::string::npos) end = name.find(']', start);
    if (start != std::string::npos && end != std::string::npos) {
        return name.substr(start, end - start);
    }
#elif defined(_MSC_VER)
    std::string name = __FUNCSIG__;
    auto start = name.find("extract_type_name<") + 18;
    auto end = name.find('>', start);
    if (start != std::string::npos && end != std::string::npos) {
        return name.substr(start, end - start);
    }
#endif
    return typeid(T).name();
}

}

export template<typename T>
inline CapabilityRegistrar<T> registered{detail::extract_type_name<T>()};

}
