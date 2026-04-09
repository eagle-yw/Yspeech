module;

export module yspeech.aspect;

import std;
import yspeech.runtime.runtime_context;

namespace yspeech {

export template <typename T>
concept Aspect = requires(T t, RuntimeContext& runtime, const std::string& component_name, std::any payload) {
    { t.before(runtime, component_name) } -> std::same_as<std::any>;
    { t.after(runtime, component_name, payload) } -> std::same_as<void>;
};

export class AspectIface {
public:
    template<Aspect T>
    AspectIface(T&& aspect): self_(std::make_unique<Model<std::remove_cvref_t<T>>>(std::forward<T>(aspect))) {}

    AspectIface(const AspectIface& other)
        : self_(other.self_ ? other.self_->clone() : nullptr) {}

    AspectIface& operator=(const AspectIface& other) {
        if (this != &other) {
            self_ = other.self_ ? other.self_->clone() : nullptr;
        }
        return *this;
    }

    AspectIface(AspectIface&&) noexcept = default;
    AspectIface& operator=(AspectIface&&) noexcept = default;

    auto before(RuntimeContext& runtime, const std::string& component_name) -> std::any {
        return self_->before(runtime, component_name);
    }

    auto after(RuntimeContext& runtime, const std::string& component_name, std::any payload) -> void {
        self_->after(runtime, component_name, std::move(payload));
    }

    template <typename T>
    auto as() -> T* {
        if (self_->type() == typeid(T)) {
            return &static_cast<Model<T>*>(self_.get())->aspect_;
        }
        return nullptr;
    }

    struct Concept {
        virtual ~Concept() = default;
        virtual auto before(RuntimeContext& runtime, const std::string& component_name) -> std::any = 0;
        virtual auto after(RuntimeContext& runtime, const std::string& component_name, std::any payload) -> void = 0;
        virtual auto type() const -> const std::type_info& = 0;
        virtual auto clone() const -> std::unique_ptr<Concept> = 0;
    };

    template<Aspect T>
    struct Model: Concept {
        Model(const T& aspect): aspect_(aspect) {}
        Model(T&& aspect): aspect_(std::move(aspect)) {}

        auto before(RuntimeContext& runtime, const std::string& component_name) -> std::any override {
            return aspect_.before(runtime, component_name);
        }

        auto after(RuntimeContext& runtime, const std::string& component_name, std::any payload) -> void override {
            aspect_.after(runtime, component_name, std::move(payload));
        }

        auto type() const -> const std::type_info& override {
            return typeid(T);
        }

        auto clone() const -> std::unique_ptr<Concept> override {
            return std::make_unique<Model<T>>(aspect_);
        }

        T aspect_;
    };

private:
    std::unique_ptr<Concept> self_;
};

}
