module;

#include <nlohmann/json.hpp>

export module yspeech.runtime.pipeline_recipe;

import std;
import yspeech.pipeline_config;

namespace yspeech {

export enum class PipelineStageRole {
    Source,
    Vad,
    Feature,
    Asr,
    Event,
    Unknown
};

export enum class RuntimeNodeKind {
    Linear,
    Branch,
    Join,
    Isolated
};

export enum class JoinPolicy {
    AllOf,
    AnyOf
};

export struct PipelineStageRecipe {
    std::string stage_id;
    PipelineStageRole role = PipelineStageRole::Unknown;
    RuntimeNodeKind node_kind = RuntimeNodeKind::Linear;
    std::size_t max_concurrency = 1;
    bool enabled = true;
    std::vector<std::string> core_ids;
    std::vector<std::string> core_names;
    std::vector<std::string> depends_on;
    std::vector<std::string> downstream_ids;
    JoinPolicy join_policy = JoinPolicy::AllOf;
    std::int64_t join_timeout_ms = -1;
};

export struct PipelineRuntimeRecipe {
    std::string pipeline_name = "yspeech.streaming_pipeline";
    std::size_t num_lines = 2;
    std::vector<PipelineStageRecipe> stages;

    auto stage_by_role(PipelineStageRole role) const -> const PipelineStageRecipe* {
        for (const auto& stage : stages) {
            if (stage.role == role) {
                return &stage;
            }
        }
        return nullptr;
    }

    bool has_role(PipelineStageRole role) const {
        return stage_by_role(role) != nullptr;
    }

    auto stage_by_id(std::string_view stage_id) const -> const PipelineStageRecipe* {
        for (const auto& stage : stages) {
            if (stage.stage_id == stage_id) {
                return &stage;
            }
        }
        return nullptr;
    }
};

namespace detail {

inline auto source_core_name_from_config(const nlohmann::json& raw_config) -> std::string {
    if (!raw_config.contains("source") || !raw_config["source"].is_object()) {
        return "MicrophoneSource";
    }
    const auto& source = raw_config["source"];
    const auto type = source.value("type", std::string("microphone"));
    if (type == "file") {
        return "FileSource";
    }
    if (type == "stream") {
        return "StreamSource";
    }
    return "MicrophoneSource";
}

inline auto infer_stage_role(const PipelineStageConfig& stage) -> PipelineStageRole {
    bool has_source = false;
    bool has_vad = false;
    bool has_feature = false;
    bool has_asr = false;

    for (const auto& op : stage.ops()) {
        if (op.name == "PassThroughSource" ||
            op.name == "FileSource" ||
            op.name == "MicrophoneSource" ||
            op.name == "StreamSource") {
            has_source = true;
        } else if (op.name == "SileroVad") {
            has_vad = true;
        } else if (op.name == "KaldiFbank") {
            has_feature = true;
        } else if (op.name == "AsrParaformer" ||
                   op.name == "AsrSenseVoice" ||
                   op.name == "AsrWhisper") {
            has_asr = true;
        }
    }

    const int matched =
        static_cast<int>(has_source) + static_cast<int>(has_vad) + static_cast<int>(has_feature) + static_cast<int>(has_asr);
    if (matched != 1) {
        return PipelineStageRole::Unknown;
    }
    if (has_source) {
        return PipelineStageRole::Source;
    }
    if (has_vad) {
        return PipelineStageRole::Vad;
    }
    if (has_feature) {
        return PipelineStageRole::Feature;
    }
    return PipelineStageRole::Asr;
}

inline auto parse_join_policy(const nlohmann::json& stage_json) -> JoinPolicy {
    if (!stage_json.contains("join_policy") || !stage_json["join_policy"].is_string()) {
        return JoinPolicy::AllOf;
    }

    const auto policy = stage_json["join_policy"].get<std::string>();
    if (policy == "any_of") {
        return JoinPolicy::AnyOf;
    }
    return JoinPolicy::AllOf;
}

inline auto parse_join_timeout_ms(const nlohmann::json& stage_json) -> std::int64_t {
    if (!stage_json.contains("join_timeout_ms") || !stage_json["join_timeout_ms"].is_number_integer()) {
        return -1;
    }
    return std::max<std::int64_t>(-1, stage_json["join_timeout_ms"].get<std::int64_t>());
}

} // namespace detail

export inline auto make_pipeline_runtime_recipe(
    const PipelineConfig& config,
    const nlohmann::json& raw_config,
    std::size_t fallback_num_lines = 2,
    std::string fallback_name = "yspeech.streaming_pipeline"
) -> PipelineRuntimeRecipe {
    PipelineRuntimeRecipe recipe;
    recipe.pipeline_name = config.name().empty() ? std::move(fallback_name) : config.name();
    recipe.num_lines = std::max<std::size_t>(1, fallback_num_lines);

    std::unordered_map<std::string, std::vector<std::string>> stage_dependencies;
    std::unordered_map<std::string, JoinPolicy> stage_join_policies;
    std::unordered_map<std::string, std::int64_t> stage_join_timeouts;
    if (raw_config.contains("pipelines") && raw_config["pipelines"].is_array()) {
        for (const auto& stage_json : raw_config["pipelines"]) {
            if (!stage_json.contains("id") || !stage_json["id"].is_string()) {
                continue;
            }
            auto stage_id = stage_json["id"].get<std::string>();
            auto& deps = stage_dependencies[stage_id];
            if (stage_json.contains("depends_on") && stage_json["depends_on"].is_array()) {
                for (const auto& dep : stage_json["depends_on"]) {
                    if (dep.is_string()) {
                        deps.push_back(dep.get<std::string>());
                    }
                }
            }
            stage_join_policies[stage_id] = detail::parse_join_policy(stage_json);
            stage_join_timeouts[stage_id] = detail::parse_join_timeout_ms(stage_json);
        }
    }

    for (const auto& stage : config.stages()) {
        PipelineStageRecipe entry;
        entry.stage_id = stage.id();
        entry.role = detail::infer_stage_role(stage);
        entry.max_concurrency = stage.max_concurrency();
        if (auto it = stage_dependencies.find(entry.stage_id); it != stage_dependencies.end()) {
            entry.depends_on = it->second;
        }
        if (auto it = stage_join_policies.find(entry.stage_id); it != stage_join_policies.end()) {
            entry.join_policy = it->second;
        }
        if (auto it = stage_join_timeouts.find(entry.stage_id); it != stage_join_timeouts.end()) {
            entry.join_timeout_ms = it->second;
        }
        for (const auto& op : stage.ops()) {
            entry.core_ids.push_back(op.id);
            entry.core_names.push_back(op.name);
        }
        recipe.stages.push_back(std::move(entry));
    }

    const bool has_explicit_source_stage = std::ranges::any_of(
        recipe.stages,
        [](const PipelineStageRecipe& stage) { return stage.role == PipelineStageRole::Source; }
    );
    if (!has_explicit_source_stage) {
        PipelineStageRecipe source_stage;
        source_stage.stage_id = "source_stage";
        source_stage.role = PipelineStageRole::Source;
        source_stage.max_concurrency = 1;
        source_stage.core_ids = {"source"};
        source_stage.core_names = {detail::source_core_name_from_config(raw_config)};
        recipe.stages.insert(recipe.stages.begin(), std::move(source_stage));

        for (std::size_t i = 1; i < recipe.stages.size(); ++i) {
            if (recipe.stages[i].depends_on.empty()) {
                recipe.stages[i].depends_on.push_back("source_stage");
            }
        }
    }

    std::unordered_map<std::string, std::size_t> stage_index;
    for (std::size_t i = 0; i < recipe.stages.size(); ++i) {
        stage_index.emplace(recipe.stages[i].stage_id, i);
    }

    for (auto& stage : recipe.stages) {
        for (const auto& dep_id : stage.depends_on) {
            if (auto it = stage_index.find(dep_id); it != stage_index.end()) {
                recipe.stages[it->second].downstream_ids.push_back(stage.stage_id);
            }
        }
    }

    for (auto& stage : recipe.stages) {
        const auto indegree = stage.depends_on.size();
        const auto outdegree = stage.downstream_ids.size();
        if (indegree == 0 && outdegree == 0) {
            stage.node_kind = RuntimeNodeKind::Isolated;
        } else if (indegree > 1) {
            stage.node_kind = RuntimeNodeKind::Join;
        } else if (outdegree > 1) {
            stage.node_kind = RuntimeNodeKind::Branch;
        } else {
            stage.node_kind = RuntimeNodeKind::Linear;
        }
    }

    return recipe;
}

}
