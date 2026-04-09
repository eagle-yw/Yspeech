module;

#include <nlohmann/json.hpp>

export module yspeech.runtime.runtime_context;

import std;
import yspeech.types;

namespace yspeech {

export struct RuntimeContext {
    struct StreamFeatureSnapshot {
        std::vector<std::vector<float>> features;
        std::uint64_t version = 0;
        int feature_count = 0;
    };

    nlohmann::json config;
    std::string task = "asr";
    ProcessingStats* stats = nullptr;
    std::chrono::steady_clock::time_point run_start_time{};
    std::mutex stats_mutex;
    std::mutex stream_feature_mutex;
    std::unordered_map<std::string, StreamFeatureSnapshot> stream_feature_snapshots;

    std::atomic<bool> stopping{false};
    std::atomic<bool> eos_seen{false};

    EngineEventCallback emit_event;
    StatusCallback emit_status;
    PerformanceCallback emit_performance;
    AlertCallback emit_alert;
};

}
