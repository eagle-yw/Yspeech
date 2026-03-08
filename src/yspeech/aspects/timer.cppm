module;

#include <chrono>

export module yspeech.aspect.timer;

import std;
import yspeech.context;
import yspeech.aspect;
import yspeech.log;

namespace yspeech {

export class TimerAspect {
public:
    std::any before(Context& ctx, const std::string& op_name) {
        return std::chrono::high_resolution_clock::now();
    }

    void after(Context& ctx, const std::string& op_name, std::any payload) {
        auto start = std::any_cast<std::chrono::high_resolution_clock::time_point>(payload);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        log_info("Operator {} took {} us", op_name, duration.count());
    }
};

}
