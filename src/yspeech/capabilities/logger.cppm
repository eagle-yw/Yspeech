module;

#include <nlohmann/json.hpp>

export module yspeech.capability.logger;

import std;
import yspeech.capability;
import yspeech.log;
import yspeech.runtime.runtime_context;

namespace yspeech {

export class LogCapability {
public:
    explicit LogCapability(const nlohmann::json& config = {}) {
        init(config);
    }

    void init(const nlohmann::json& config) {
        message_ = config.value("message", std::string("capability invoked"));
        component_name_ = config.value("__component_name", std::string{});
        const auto phase = config.value("phase", std::string("pre"));
        phase_ = phase == "post" ? CapabilityPhase::Post : CapabilityPhase::Pre;
    }

    void apply(RuntimeContext&) {
        if (component_name_.empty()) {
            log_info("Capability: {}", message_);
            return;
        }
        log_info("Capability [{}]: {}", component_name_, message_);
    }

    auto phase() const -> CapabilityPhase {
        return phase_;
    }

private:
    std::string message_ = "capability invoked";
    std::string component_name_;
    CapabilityPhase phase_ = CapabilityPhase::Pre;
};

CapabilityRegistrar<LogCapability> log_capability_registrar("LogCapability");

}
