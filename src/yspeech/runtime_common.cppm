module;

#include <nlohmann/json.hpp>

export module yspeech.runtime_common;

import std;
import yspeech.context;
import yspeech.log;
import yspeech.pipeline_config;
import yspeech.pipeline_manager;
import yspeech.stream_store;

namespace yspeech {

export struct RuntimeComponents {
    PipelineConfig pipeline_config;
    std::unique_ptr<StreamStore> stream_store;
    std::unique_ptr<PipelineManager> pipeline_manager;
    std::unique_ptr<Context> context;
};

export struct FrameConfig {
    int sample_rate = 16000;
    int channels = 1;
    int dur_ms = 10;
    std::size_t ring_capacity_frames = 6000;
    std::string audio_frame_key = "audio_frames";
};

export inline void apply_runtime_log_level(const nlohmann::json& config) {
    if (!config.contains("log_level")) {
        return;
    }

    std::string level_str = config["log_level"].get<std::string>();
    LogLevel level = LogLevel::Info;
    if (level_str == "debug") level = LogLevel::Debug;
    else if (level_str == "info") level = LogLevel::Info;
    else if (level_str == "warn" || level_str == "warning") level = LogLevel::Warn;
    else if (level_str == "error") level = LogLevel::Error;
    else if (level_str == "none") level = LogLevel::None;
    set_log_level(level);
}

export inline auto load_runtime_config(const std::string& config_path) -> nlohmann::json {
    if (!std::filesystem::exists(config_path)) {
        throw std::runtime_error(std::format("Configuration file not found: {}", config_path));
    }

    std::ifstream file(config_path);
    if (!file.is_open()) {
        throw std::runtime_error(std::format("Failed to open configuration file: {}", config_path));
    }

    try {
        auto config = nlohmann::json::parse(file);
        apply_runtime_log_level(config);
        return config;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::format("JSON parse error: {}", e.what()));
    }
}

export inline auto build_runtime_components(const nlohmann::json& config) -> RuntimeComponents {
    RuntimeComponents components;
    components.pipeline_config = PipelineConfig::from_json(config);
    components.stream_store = std::make_unique<StreamStore>();
    components.pipeline_manager = std::make_unique<PipelineManager>();
    components.pipeline_manager->build(components.pipeline_config);
    components.context = std::make_unique<Context>();
    return components;
}

export inline auto read_runtime_int_config(
    const nlohmann::json& config,
    std::initializer_list<std::pair<std::string_view, std::string_view>> paths,
    int default_value
) -> int {
    for (const auto& [section, key] : paths) {
        if (!config.contains(std::string(section))) {
            continue;
        }
        const auto& node = config[std::string(section)];
        if (!node.is_object() || !node.contains(std::string(key))) {
            continue;
        }
        if (!node[std::string(key)].is_number_integer()) {
            continue;
        }
        return node[std::string(key)].get<int>();
    }
    return default_value;
}

export inline auto read_frame_config(const nlohmann::json& config) -> FrameConfig {
    FrameConfig frame_config;
    frame_config.sample_rate = read_runtime_int_config(config, {
        {"frame", "sample_rate"},
        {"input", "sample_rate"}
    }, frame_config.sample_rate);
    frame_config.channels = read_runtime_int_config(config, {
        {"frame", "channels"}
    }, frame_config.channels);
    frame_config.dur_ms = read_runtime_int_config(config, {
        {"frame", "dur_ms"}
    }, frame_config.dur_ms);
    frame_config.ring_capacity_frames = static_cast<std::size_t>(read_runtime_int_config(config, {
        {"stream", "ring_capacity_frames"}
    }, static_cast<int>(frame_config.ring_capacity_frames)));
    return frame_config;
}

}
