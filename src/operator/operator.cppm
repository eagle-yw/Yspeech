export module yspeech.Operator;

import std;
import yspeech.Common.Types;
import yspeech.Context;


namespace yspeech {



template <typename T>
concept Operator = requires(T t) {
    { t.process(Context{}) } -> std::same_as<void>;
    { t.load(std::string_view{}) } -> std::same_as<void>;
} && NonCopyable<T> && Movable<T>;

export class OperatorIface {
public:
    template<Operator T>
    OperatorIface(T&& op): self_(std::make_unique<Model<T>>(std::forward<T>(op))) {}

    OperatorIface(const OperatorIface&) = delete;
    OperatorIface& operator=(const OperatorIface&) = delete;
    OperatorIface(OperatorIface&&) noexcept = default;
    OperatorIface& operator=(OperatorIface&&) noexcept = default;

    auto process(Context data) -> void {
        self_->process(data);
    }

    template <typename T>
    auto as() -> T* {
        if (self_->type() == typeid(T)) {
            return &static_cast<Model<T>*>(self_.get())->op_;
        }
        return nullptr;
    }

private:
    struct Concept {
        virtual ~Concept() = default;
        virtual auto process(Context data) -> void = 0;
        virtual auto type() const -> const std::type_info& = 0;
    };

    template<Operator T>
    struct Model: Concept {        
        Model(T&& op): op_(std::move(op)) {}

        auto process(Context data)->void override {
            op_.process(data);
        }

        auto type() const -> const std::type_info& override {
            return typeid(T);
        }
        auto get() -> T& { return op_; }

        T op_;
    };

    std::unique_ptr<Concept> self_;
};


}