module;

#include <nlohmann/json.hpp>

export module yspeech.runtime.common;

import std;
import yspeech.log;
import yspeech.pipeline_config;

namespace yspeech {

namespace detail {

inline auto is_placeholder_source_value(const nlohmann::json& value) -> bool {
    return value.is_string() && value.get<std::string>().empty() == false &&
           value.get<std::string>() == "__AUDIO_PATH__";
}

inline auto source_type_from_core_name(std::string_view core_name) -> std::optional<std::string> {
    if (core_name == "FileSource") {
        return "file";
    }
    if (core_name == "MicrophoneSource") {
        return "microphone";
    }
    if (core_name == "StreamSource") {
        return "stream";
    }
    return std::nullopt;
}

inline auto find_source_stage_op(nlohmann::json& config) -> nlohmann::json* {
    if (!config.contains("pipelines") || !config["pipelines"].is_array()) {
        return nullptr;
    }

    for (auto& stage : config["pipelines"]) {
        if (!stage.is_object() || !stage.contains("ops") || !stage["ops"].is_array() || stage["ops"].empty()) {
            continue;
        }
        auto& op = stage["ops"][0];
        if (!op.is_object() || !op.contains("name") || !op["name"].is_string()) {
            continue;
        }
        if (source_type_from_core_name(op["name"].get<std::string>()).has_value()) {
            return &op;
        }
    }

    return nullptr;
}

inline auto find_source_stage_op(const nlohmann::json& config) -> const nlohmann::json* {
    if (!config.contains("pipelines") || !config["pipelines"].is_array()) {
        return nullptr;
    }

    for (const auto& stage : config["pipelines"]) {
        if (!stage.is_object() || !stage.contains("ops") || !stage["ops"].is_array() || stage["ops"].empty()) {
            continue;
        }
        const auto& op = stage["ops"][0];
        if (!op.is_object() || !op.contains("name") || !op["name"].is_string()) {
            continue;
        }
        if (source_type_from_core_name(op["name"].get<std::string>()).has_value()) {
            return &op;
        }
    }

    return nullptr;
}

} // namespace detail

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

export inline void apply_file_source_override(
    nlohmann::json& config,
    const std::string& audio_path,
    std::optional<double> playback_rate = std::nullopt
) {
    if (auto* op = detail::find_source_stage_op(config); op != nullptr) {
        (*op)["name"] = "FileSource";
        auto& params = (*op)["params"];
        if (!params.is_object()) {
            params = nlohmann::json::object();
        }
        params["path"] = audio_path;
        if (playback_rate.has_value()) {
            params["playback_rate"] = *playback_rate;
        }
        return;
    }

    if (!config.contains("source") || !config["source"].is_object()) {
        config["source"] = nlohmann::json::object();
    }
    auto& source = config["source"];
    source["type"] = "file";
    source["path"] = audio_path;
    if (playback_rate.has_value()) {
        source["playback_rate"] = *playback_rate;
    }
}

export inline auto load_runtime_config_with_file_source(
    const std::string& config_path,
    const std::string& audio_path,
    std::optional<double> playback_rate = std::nullopt
) -> nlohmann::json {
    auto config = load_runtime_config(config_path);
    apply_file_source_override(config, audio_path, playback_rate);
    return config;
}

export inline auto read_source_config(const nlohmann::json& config) -> nlohmann::json {
    nlohmann::json source = nlohmann::json::object();
    if (config.contains("source") && config["source"].is_object()) {
        source = config["source"];
    }

    if (const auto* op = detail::find_source_stage_op(config); op != nullptr) {
        const auto core_name = (*op).value("name", std::string{});
        if (auto source_type = detail::source_type_from_core_name(core_name); source_type.has_value()) {
            source["type"] = *source_type;
        }
        if ((*op).contains("params") && (*op)["params"].is_object()) {
            for (const auto& [key, value] : (*op)["params"].items()) {
                if (key == "path" &&
                    (value.is_string() && (value.get<std::string>().empty() || detail::is_placeholder_source_value(value))) &&
                    source.contains(key) && source[key].is_string() && !source[key].get<std::string>().empty() &&
                    !detail::is_placeholder_source_value(source[key])) {
                    continue;
                }
                source[key] = value;
            }
        }
        if (source.contains("type")) {
            return source;
        }
    }

    if (!source.empty()) {
        return source;
    }

    return nlohmann::json::object();
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
