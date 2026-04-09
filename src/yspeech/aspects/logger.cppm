module;

export module yspeech.aspect.logger;

import std;
import yspeech.aspect;
import yspeech.log;
import yspeech.runtime.runtime_context;

namespace yspeech {

export class LoggerAspect {
public:
    std::any before(RuntimeContext& runtime, const std::string& component_name) {
        log_debug("Entering processing node: {}", component_name);
        return {};
    }

    void after(RuntimeContext& runtime, const std::string& component_name, std::any payload) {
        log_debug("Exiting processing node: {}", component_name);
    }
};

}
