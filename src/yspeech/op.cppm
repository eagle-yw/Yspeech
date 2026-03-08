module;

#include <nlohmann/json.hpp>
#include <typeinfo>

export module yspeech.op;

import std;
import yspeech.context;
import yspeech.capability;

namespace yspeech {

using json = nlohmann::json;

export template <typename T>
concept Operator = requires(T t, Context& ctx, const json& config) {
    { t.init(config) } -> std::same_as<void>;
    { t.process(ctx) } -> std::same_as<void>;    
};

export class OperatorIface {
public:
    template<Operator T>
    OperatorIface(T&& op): self_(std::make_unique<Model<std::remove_cvref_t<T>>>(std::forward<T>(op))) {}

    OperatorIface(const OperatorIface&) = delete;
    OperatorIface& operator=(const OperatorIface&) = delete;
    OperatorIface(OperatorIface&&) noexcept = default;
    OperatorIface& operator=(OperatorIface&&) noexcept = default;

    auto init(const json& config) -> void {
        self_->init(config);
        install_capabilities_from_config(config);
    }

    auto process(Context& ctx) -> void {
        apply_capabilities(ctx, CapabilityPhase::Pre);
        self_->process(ctx);
        apply_capabilities(ctx, CapabilityPhase::Post);
    }

    auto deinit() -> void {
        self_->deinit();
        uninstall_all();
    }

    template <typename T>
    auto as() -> T* {
        if (self_->type() == typeid(T)) {
            return &static_cast<Model<T>*>(self_.get())->op_;
        }
        return nullptr;
    }

    template<Capability T>
    void install(T&& cap, std::string name = "") {
        if (name.empty()) {
            name = typeid(T).name();
        }
        capabilities_.emplace(name, CapabilityIface(std::forward<T>(cap), name));
        capability_order_.push_back(std::move(name));
    }

    void install(std::string name, const json& config = {}) {
        capabilities_.emplace(name, CapabilityFactory::get_instance().create_capability(name, config));
        capability_order_.push_back(name);
    }

    bool uninstall(const std::string& name) {
        if (!capabilities_.contains(name)) {
            return false;
        }
        capabilities_.erase(name);
        std::erase(capability_order_, name);
        return true;
    }

    void uninstall_all() {
        capabilities_.clear();
        capability_order_.clear();
    }

    bool has_capability(const std::string& name) const {
        return capabilities_.contains(name);
    }

    auto capability_count() const -> std::size_t {
        return capabilities_.size();
    }

    auto list_capabilities() const -> std::vector<std::string> {
        return capability_order_;
    }

    struct Concept {
        virtual ~Concept() = default;
        virtual auto init(const json& config) -> void = 0;
        virtual auto process(Context& ctx) -> void = 0;
        virtual auto deinit() -> void = 0;
        virtual auto type() const -> const std::type_info& = 0;
    };

    template<Operator T>
    struct Model: Concept {
        Model(const T& op): op_(op) {}
        Model(T&& op): op_(std::move(op)) {}

        auto init(const json& config) -> void override {
            op_.init(config);
        }

        auto process(Context& ctx) -> void override {
            op_.process(ctx);
        }

        auto deinit() -> void override {
            if constexpr (requires { op_.deinit(); }) {
                op_.deinit();
            }
        }

        auto type() const -> const std::type_info& override {
            return typeid(T);
        }
        
        T op_;
    };

private:
    std::unique_ptr<Concept> self_;
    std::unordered_map<std::string, CapabilityIface> capabilities_;
    std::vector<std::string> capability_order_;

    auto apply_capabilities(Context& ctx, CapabilityPhase phase) -> void {
        for (const auto& name : capability_order_) {
            if (capabilities_.contains(name) && capabilities_.at(name).phase() == phase) {
                capabilities_.at(name).apply(ctx);
            }
        }
    }

    void install_capabilities_from_config(const json& config) {
        if (!config.contains("capabilities")) {
            return;
        }
        
        for (const auto& cap_config : config["capabilities"]) {
            std::string name = cap_config["name"];
            json params = cap_config.value("params", json::object());
            install(name, params);
        }
    }
};

export class OperatorFactory {
public:
    using CreatorFunc = std::function<OperatorIface()>;

    static OperatorFactory& get_instance() {
        static OperatorFactory instance;
        return instance;
    }

    void register_operator(const std::string& name, CreatorFunc creator) {
        if (registry_.contains(name)) {
            throw std::runtime_error(std::format("Operator type already registered: {}", name));
        }
        registry_[name] = std::move(creator);
    }

    OperatorIface create_operator(const std::string& name) {
        if (!registry_.contains(name)) {
            throw std::runtime_error(std::format("Unknown operator type: {}", name));
        }
        return registry_[name]();
    }

private:
    OperatorFactory() = default;
    std::unordered_map<std::string, CreatorFunc> registry_;
};

export template<typename T>
struct OperatorRegistrar {
    OperatorRegistrar(const std::string& name) {
        OperatorFactory::get_instance().register_operator(name, []() -> OperatorIface {
            return OperatorIface(T());
        });
    }
};

}
