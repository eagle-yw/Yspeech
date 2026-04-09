module;

export module yspeech.runtime.runtime_dag;

import std;
import yspeech.runtime.pipeline_recipe;

namespace yspeech {

export using RuntimeDagNodeId = std::string;

export struct RuntimeDagNode {
    RuntimeDagNodeId node_id;
    PipelineStageRole role = PipelineStageRole::Unknown;
    RuntimeNodeKind node_kind = RuntimeNodeKind::Linear;
    JoinPolicy join_policy = JoinPolicy::AllOf;
    std::int64_t join_timeout_ms = -1;
    std::vector<std::string> depends_on;
    std::vector<std::string> downstream_ids;
    std::size_t stage_path_index = 0;
    std::size_t position_in_path = 0;
};

export struct RuntimeDagPlan {
    std::string name = "yspeech.runtime_dag";
    std::vector<RuntimeDagNode> nodes;
    std::vector<std::string> root_ids;
    std::vector<std::vector<std::string>> stage_paths;

    auto node_by_id(std::string_view node_id) const -> const RuntimeDagNode* {
        for (const auto& node : nodes) {
            if (node.node_id == node_id) {
                return &node;
            }
        }
        return nullptr;
    }
};

namespace detail {

inline auto append_stage_path_from(
    const PipelineRuntimeRecipe& recipe,
    std::string start_id,
    std::unordered_set<std::string>& visited,
    RuntimeDagPlan& plan
) -> void {
    std::vector<std::string> segment;
    auto current_id = std::move(start_id);

    while (true) {
        const auto* stage = recipe.stage_by_id(current_id);
        if (!stage || visited.contains(current_id)) {
            break;
        }

        visited.insert(current_id);
        segment.push_back(current_id);

        if (stage->downstream_ids.size() != 1) {
            break;
        }

        const auto* next = recipe.stage_by_id(stage->downstream_ids.front());
        if (!next || next->depends_on.size() != 1) {
            break;
        }

        current_id = next->stage_id;
    }

    if (!segment.empty()) {
        plan.stage_paths.push_back(std::move(segment));
    }
}

} // namespace detail

export inline auto make_runtime_dag_plan(const PipelineRuntimeRecipe& recipe) -> RuntimeDagPlan {
    RuntimeDagPlan plan;
    plan.name = recipe.pipeline_name;

    for (const auto& stage : recipe.stages) {
        RuntimeDagNode node;
        node.node_id = stage.stage_id;
        node.role = stage.role;
        node.node_kind = stage.node_kind;
        node.join_policy = stage.join_policy;
        node.join_timeout_ms = stage.join_timeout_ms;
        node.depends_on = stage.depends_on;
        node.downstream_ids = stage.downstream_ids;
        plan.nodes.push_back(std::move(node));
        if (stage.depends_on.empty()) {
            plan.root_ids.push_back(stage.stage_id);
        }
    }

    std::unordered_set<std::string> visited;
    for (const auto& root_id : plan.root_ids) {
        detail::append_stage_path_from(recipe, root_id, visited, plan);
    }
    for (const auto& stage : recipe.stages) {
        if (!visited.contains(stage.stage_id) &&
            (stage.depends_on.size() != 1 || stage.node_kind != RuntimeNodeKind::Linear)) {
            detail::append_stage_path_from(recipe, stage.stage_id, visited, plan);
        }
    }
    for (const auto& stage : recipe.stages) {
        if (!visited.contains(stage.stage_id)) {
            detail::append_stage_path_from(recipe, stage.stage_id, visited, plan);
        }
    }

    for (std::size_t path_index = 0; path_index < plan.stage_paths.size(); ++path_index) {
        const auto& path = plan.stage_paths[path_index];
        for (std::size_t position = 0; position < path.size(); ++position) {
            for (auto& node : plan.nodes) {
                if (node.node_id == path[position]) {
                    node.stage_path_index = path_index;
                    node.position_in_path = position;
                    break;
                }
            }
        }
    }

    return plan;
}

}
