module;

#include <nlohmann/json.hpp>

export module yspeech.capability.alert;

import std;
import yspeech.capability;
import yspeech.runtime.runtime_context;

namespace yspeech {

export class AlertCapability {
public:
    explicit AlertCapability(const nlohmann::json& config = {}) {
        init(config);
    }

    void init(const nlohmann::json& config) {
        alert_id_ = config.value("alert_id", std::string("capability_alert"));
        message_ = config.value("message", std::string("capability alert"));
        const auto phase = config.value("phase", std::string("pre"));
        phase_ = phase == "post" ? CapabilityPhase::Post : CapabilityPhase::Pre;
    }

    void apply(RuntimeContext& runtime) {
        if (runtime.emit_alert) {
            runtime.emit_alert(alert_id_, message_);
        }
    }

    auto phase() const -> CapabilityPhase {
        return phase_;
    }

private:
    std::string alert_id_ = "capability_alert";
    std::string message_ = "capability alert";
    CapabilityPhase phase_ = CapabilityPhase::Pre;
};

CapabilityRegistrar<AlertCapability> alert_capability_registrar("AlertCapability");

}
