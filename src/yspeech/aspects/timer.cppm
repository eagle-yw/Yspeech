module;

#include <chrono>

export module yspeech.aspect.timer;

import std;
import yspeech.aspect;
import yspeech.log;
import yspeech.types;
import yspeech.runtime.runtime_context;

namespace yspeech {

export class TimerAspect {
public:
    std::any before(RuntimeContext& runtime, const std::string& component_name) {
        return std::chrono::high_resolution_clock::now();
    }

    void after(RuntimeContext& runtime, const std::string& component_name, std::any payload) {
        auto start = std::any_cast<std::chrono::high_resolution_clock::time_point>(payload);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double duration_ms = duration_us.count() / 1000.0;

        if (runtime.stats) {
            std::scoped_lock lock(runtime.stats_mutex);
            runtime.stats->record_core_time(component_name, duration_ms);
            runtime.stats->record_core_effective_call(component_name);
            if (runtime.run_start_time != std::chrono::steady_clock::time_point{}) {
                const auto start_ms =
                    std::chrono::duration<double, std::milli>(start - runtime.run_start_time).count();
                const auto end_ms =
                    std::chrono::duration<double, std::milli>(end - runtime.run_start_time).count();
                runtime.stats->record_core_active_window(component_name, start_ms, end_ms);
            }
        }

        log_debug("Processing node {} took {:.2f} ms", component_name, duration_ms);
    }
};

}
