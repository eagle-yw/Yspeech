module;

export module yspeech.aspect;

import std;
import yspeech.context;

namespace yspeech {

export template <typename T>
concept Aspect = requires(T t, Context& ctx, const std::string& op_name, std::any payload) {
    { t.before(ctx, op_name) } -> std::same_as<std::any>;
    { t.after(ctx, op_name, payload) } -> std::same_as<void>;
};

export class AspectIface {
public:
    template<Aspect T>
    AspectIface(T&& aspect): self_(std::make_unique<Model<std::remove_cvref_t<T>>>(std::forward<T>(aspect))) {}

    AspectIface(const AspectIface&) = delete;
    AspectIface& operator=(const AspectIface&) = delete;
    AspectIface(AspectIface&&) noexcept = default;
    AspectIface& operator=(AspectIface&&) noexcept = default;

    auto before(Context& ctx, const std::string& op_name) -> std::any {
        return self_->before(ctx, op_name);
    }

    auto after(Context& ctx, const std::string& op_name, std::any payload) -> void {
        self_->after(ctx, op_name, std::move(payload));
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
        virtual auto before(Context& ctx, const std::string& op_name) -> std::any = 0;
        virtual auto after(Context& ctx, const std::string& op_name, std::any payload) -> void = 0;
        virtual auto type() const -> const std::type_info& = 0;
    };

    template<Aspect T>
    struct Model: Concept {
        Model(const T& aspect): aspect_(aspect) {}
        Model(T&& aspect): aspect_(std::move(aspect)) {}

        auto before(Context& ctx, const std::string& op_name) -> std::any override {
            return aspect_.before(ctx, op_name);
        }

        auto after(Context& ctx, const std::string& op_name, std::any payload) -> void override {
            aspect_.after(ctx, op_name, std::move(payload));
        }

        auto type() const -> const std::type_info& override {
            return typeid(T);
        }

        T aspect_;
    };

private:
    std::unique_ptr<Concept> self_;
};

}
