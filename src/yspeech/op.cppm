module;

#include <nlohmann/json.hpp>

export module yspeech.op;

import std;
import yspeech.context;
import yspeech.capability;
import yspeech.stream_store;

namespace yspeech {

using json = nlohmann::json;

export template <typename T>
concept OperatorConfigurable = requires(T t, const json& config) {
    { t.init(config) } -> std::same_as<void>;
};

export enum class StreamProcessStatus {
    NoOp,
    ConsumedInput,
    ProducedOutput,
    SegmentFinalized,
    StreamFinalized,
    NeedMoreInput,
    OverrunRecovered
};

export struct StreamProcessResult {
    StreamProcessStatus status = StreamProcessStatus::NoOp;
    std::size_t consumed_frames = 0;
    std::size_t produced_items = 0;
    bool wake_downstream = false;
};

export template <typename T>
concept OperatorStreamReady = requires(T t, Context& ctx, StreamStore& store) {
    { t.ready(ctx, store) } -> std::convertible_to<bool>;
};

export template <typename T>
concept OperatorStreamProcess = requires(T t, Context& ctx, StreamStore& store) {
    { t.process_stream(ctx, store) } -> std::same_as<StreamProcessResult>;
};

export template <typename T>
concept OperatorStreamFlush = requires(T t, Context& ctx, StreamStore& store) {
    { t.flush(ctx, store) } -> std::same_as<StreamProcessResult>;
};

export template <typename T>
concept OperatorDeinitializable = requires(T t) {
    { t.deinit() } -> std::same_as<void>;
};

export template <typename T>
concept OperatorImplementation =
    OperatorConfigurable<T> &&
    OperatorStreamProcess<T>;

export class OperatorIface {
public:
    template<OperatorImplementation T>
    OperatorIface(T&& op): self_(std::make_unique<Model<std::remove_cvref_t<T>>>(std::forward<T>(op))) {}

    OperatorIface(const OperatorIface&) = delete;
    OperatorIface& operator=(const OperatorIface&) = delete;
    OperatorIface(OperatorIface&&) noexcept = default;
    OperatorIface& operator=(OperatorIface&&) noexcept = default;

    auto init(const json& config) -> void {
        self_->init(config);
        install_capabilities_from_config(config);
    }

    auto ready_stream(Context& ctx, StreamStore& store) -> bool {
        return self_->ready_stream(ctx, store);
    }

    auto process_stream(Context& ctx, StreamStore& store) -> StreamProcessResult {
        apply_capabilities(ctx, CapabilityPhase::Pre);
        auto result = self_->process_stream(ctx, store);
        apply_capabilities(ctx, CapabilityPhase::Post);
        return result;
    }

    auto flush_stream(Context& ctx, StreamStore& store) -> StreamProcessResult {
        apply_capabilities(ctx, CapabilityPhase::Pre);
        auto result = self_->flush_stream(ctx, store);
        apply_capabilities(ctx, CapabilityPhase::Post);
        return result;
    }

    auto supports_flush() const -> bool {
        return self_->supports_flush();
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
        virtual auto ready_stream(Context& ctx, StreamStore& store) -> bool = 0;
        virtual auto process_stream(Context& ctx, StreamStore& store) -> StreamProcessResult = 0;
        virtual auto flush_stream(Context& ctx, StreamStore& store) -> StreamProcessResult = 0;
        virtual auto supports_flush() const -> bool = 0;
        virtual auto deinit() -> void = 0;
        virtual auto type() const -> const std::type_info& = 0;
    };

    template<OperatorImplementation T>
    struct Model: Concept {
        Model(const T& op): op_(op) {}
        Model(T&& op): op_(std::move(op)) {}

        auto init(const json& config) -> void override {
            op_.init(config);
        }

        auto ready_stream(Context& ctx, StreamStore& store) -> bool override {
            if constexpr (OperatorStreamReady<T>) {
                return op_.ready(ctx, store);
            } else {
                return true;
            }
        }

        auto process_stream(Context& ctx, StreamStore& store) -> StreamProcessResult override {
            return op_.process_stream(ctx, store);
        }

        auto flush_stream(Context& ctx, StreamStore& store) -> StreamProcessResult override {
            if constexpr (OperatorStreamFlush<T>) {
                return op_.flush(ctx, store);
            } else {
                return {};
            }
        }

        auto supports_flush() const -> bool override {
            return OperatorStreamFlush<T>;
        }

        auto deinit() -> void override {
            if constexpr (OperatorDeinitializable<T>) {
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
