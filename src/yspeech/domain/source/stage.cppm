module;

#include <nlohmann/json.hpp>

export module yspeech.domain.source.stage;

import std;
import yspeech.aspect;
import yspeech.aspect.logger;
import yspeech.aspect.timer;
import yspeech.capability;
import yspeech.domain.source.base;
import yspeech.domain.source.builtin;
import yspeech.runtime.runtime_context;
import yspeech.runtime.segment_registry;
import yspeech.runtime.token;
import yspeech.types;

namespace yspeech {

export class SourceStage {
public:
    void init(const nlohmann::json& config) {
        config_ = config;
        core_name_ = config.value("core_name", std::string("PassThroughSource"));
        core_id_ = config.value("__core_id", core_id_);
        core_ = SourceCoreFactory::get_instance().create_core(core_name_);
        core_->init(config);
        configure_aspects(config);
        configure_capabilities(config);
    }

    void process(PipelineToken& token, RuntimeContext& runtime, SegmentRegistry&) {
        if (!core_) {
            return;
        }

        apply_capabilities(pre_capabilities_, runtime);
        auto aspect_payloads = before_aspects(runtime);
        core_->process(token, runtime);
        after_aspects(runtime, std::move(aspect_payloads));
        apply_capabilities(post_capabilities_, runtime);
    }

    void deinit() {
        if (core_) {
            core_->deinit();
        }
        core_.reset();
    }

private:
    nlohmann::json config_;
    std::string core_name_ = "PassThroughSource";
    std::string core_id_ = "source";
    std::unique_ptr<SourceCoreIface> core_;
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
            params["__component_name"] = core_id_;
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
            payloads.push_back(aspect.before(runtime, core_id_));
        }
        return payloads;
    }

    void after_aspects(RuntimeContext& runtime, std::vector<std::any> payloads) {
        for (std::size_t i = 0; i < aspects_.size(); ++i) {
            aspects_[i].after(runtime, core_id_, payloads[i]);
        }
    }

    static void apply_capabilities(std::vector<CapabilityIface>& capabilities, RuntimeContext& runtime) {
        for (auto& capability : capabilities) {
            capability.apply(runtime);
        }
    }
};

}
