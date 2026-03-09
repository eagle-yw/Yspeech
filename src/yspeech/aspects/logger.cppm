module;

export module yspeech.aspect.logger;

import std;
import yspeech.context;
import yspeech.aspect;
import yspeech.log;

namespace yspeech {

export class LoggerAspect {
public:
    std::any before(Context& ctx, const std::string& op_name) {
        log_debug("Entering operator: {}", op_name);
        return {};
    }

    void after(Context& ctx, const std::string& op_name, std::any payload) {
        log_debug("Exiting operator: {}", op_name);
    }
};

}
