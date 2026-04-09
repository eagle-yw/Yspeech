module;

#include <nlohmann/json.hpp>

export module yspeech.capability.status;

import std;
import yspeech.capability;
import yspeech.runtime.runtime_context;

namespace yspeech {

export class StatusCapability {
public:
    explicit StatusCapability(const nlohmann::json& config = {}) {
        init(config);
    }

    void init(const nlohmann::json& config) {
        status_ = config.value("status", std::string("capability"));
        const auto phase = config.value("phase", std::string("pre"));
        phase_ = phase == "post" ? CapabilityPhase::Post : CapabilityPhase::Pre;
    }

    void apply(RuntimeContext& runtime) {
        if (runtime.emit_status) {
            runtime.emit_status(status_);
        }
    }

    auto phase() const -> CapabilityPhase {
        return phase_;
    }

private:
    std::string status_ = "capability";
    CapabilityPhase phase_ = CapabilityPhase::Pre;
};

CapabilityRegistrar<StatusCapability> status_capability_registrar("StatusCapability");

}
