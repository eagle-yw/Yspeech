module;

#include <nlohmann/json.hpp>

export module yspeech.runtime.stage_adapter;

import std;
import yspeech.aspect;
import yspeech.aspect.logger;
import yspeech.aspect.timer;
import yspeech.capability;
import yspeech.runtime.runtime_context;

namespace yspeech {

export class StageAdapter {
public:
    void init(const nlohmann::json& config, std::string component_id) {
        config_ = config;
        component_id_ = std::move(component_id);
        configure_aspects(config_);
        configure_capabilities(config_);
    }

    void reset() {
        config_ = nlohmann::json::object();
        component_id_.clear();
        aspects_.clear();
        pre_capabilities_.clear();
        post_capabilities_.clear();
    }

    const nlohmann::json& config() const {
        return config_;
    }

    const std::string& component_id() const {
        return component_id_;
    }

    template <typename Fn>
    decltype(auto) run(RuntimeContext& runtime, Fn&& fn) {
        apply_capabilities(pre_capabilities_, runtime);
        auto aspect_payloads = before_aspects(runtime);

        if constexpr (std::is_void_v<std::invoke_result_t<Fn>>) {
            std::invoke(std::forward<Fn>(fn));
            after_aspects(runtime, std::move(aspect_payloads));
            apply_capabilities(post_capabilities_, runtime);
        } else {
            auto result = std::invoke(std::forward<Fn>(fn));
            after_aspects(runtime, std::move(aspect_payloads));
            apply_capabilities(post_capabilities_, runtime);
            return result;
        }
    }

private:
    nlohmann::json config_ = nlohmann::json::object();
    std::string component_id_;
    std::vector<AspectIface> aspects_;
    std::vector<CapabilityIface> pre_capabilities_;
    std::vector<CapabilityIface> post_capabilities_;

    void configure_aspects(const nlohmann::json& config) {
        aspects_.clear();
        aspects_.emplace_back(TimerAspect{});
        if (config.contains("aspects") && config["aspects"].is_array()) {
            for (const auto& entry : config["aspects"]) {
                if (!entry.is_string()) {
                    continue;
                }
                if (entry.get<std::string>() == "LoggerAspect") {
                    aspects_.emplace_back(LoggerAspect{});
                }
            }
        }
    }

    void configure_capabilities(const nlohmann::json& config) {
        pre_capabilities_.clear();
        post_capabilities_.clear();
        if (!config.contains("capabilities") || !config["capabilities"].is_array()) {
            return;
        }
        for (const auto& entry : config["capabilities"]) {
            if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string()) {
                continue;
            }
            auto params = entry.contains("params") && entry["params"].is_object()
                ? entry["params"]
                : nlohmann::json::object();
            params["__component_name"] = component_id_;
            auto capability = CapabilityFactory::get_instance().create_capability(
                entry["name"].get<std::string>(),
                params
            );
            if (capability.phase() == CapabilityPhase::Post) {
                post_capabilities_.push_back(std::move(capability));
            } else {
                pre_capabilities_.push_back(std::move(capability));
            }
        }
    }

    auto before_aspects(RuntimeContext& runtime) -> std::vector<std::any> {
        std::vector<std::any> payloads;
        payloads.reserve(aspects_.size());
        for (auto& aspect : aspects_) {
            payloads.push_back(aspect.before(runtime, component_id_));
        }
        return payloads;
    }

    void after_aspects(RuntimeContext& runtime, std::vector<std::any> payloads) {
        for (std::size_t i = aspects_.size(); i > 0; --i) {
            aspects_[i - 1].after(runtime, component_id_, std::move(payloads[i - 1]));
        }
    }

    static void apply_capabilities(std::vector<CapabilityIface>& capabilities, RuntimeContext& runtime) {
        for (auto& capability : capabilities) {
            capability.apply(runtime);
        }
    }
};

}
