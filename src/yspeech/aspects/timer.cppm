module;

#include <chrono>

export module yspeech.aspect.timer;

import std;
import yspeech.context;
import yspeech.aspect;
import yspeech.log;
import yspeech.types;

namespace yspeech {

export class TimerAspect {
public:
    std::any before(Context& ctx, const std::string& op_name) {
        return std::chrono::high_resolution_clock::now();
    }

    void after(Context& ctx, const std::string& op_name, std::any payload) {
        auto start = std::any_cast<std::chrono::high_resolution_clock::time_point>(payload);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double duration_ms = duration_us.count() / 1000.0;
        
        ctx.record_operator_time(op_name, duration_ms);
        
        log_debug("Operator {} took {:.2f} ms", op_name, duration_ms);
    }
};

}
