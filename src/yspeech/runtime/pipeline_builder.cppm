module;

#include <nlohmann/json.hpp>

export module yspeech.runtime.pipeline_builder;

import std;
import yspeech.pipeline_config;
import yspeech.runtime.runtime_dag;
import yspeech.runtime.pipeline_recipe;

namespace yspeech {

export struct PipelineBuilderConfig {
    std::size_t num_lines = 2;
    std::size_t queue_capacity = 8;
    std::string name = "yspeech.streaming_pipeline";
    PipelineRuntimeRecipe recipe;
    RuntimeDagPlan dag_plan;
};

export inline auto make_pipeline_builder_config(const nlohmann::json& config) -> PipelineBuilderConfig {
    PipelineBuilderConfig result;

    if (config.contains("runtime") && config["runtime"].is_object()) {
        const auto& runtime = config["runtime"];
        if (runtime.contains("pipeline_lines") && runtime["pipeline_lines"].is_number_integer()) {
            result.num_lines = std::max<std::size_t>(1, runtime["pipeline_lines"].get<std::size_t>());
        }
        if (runtime.contains("pipeline_name") && runtime["pipeline_name"].is_string()) {
            result.name = runtime["pipeline_name"].get<std::string>();
        }
        if (runtime.contains("pipeline_queue_capacity") && runtime["pipeline_queue_capacity"].is_number_integer()) {
            result.queue_capacity = std::max<std::size_t>(1, runtime["pipeline_queue_capacity"].get<std::size_t>());
        }
    }

    if (result.num_lines == 0) {
        result.num_lines = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    }
    if (result.queue_capacity == 0) {
        result.queue_capacity = std::max<std::size_t>(8, result.num_lines * 4);
    }
    result.queue_capacity = std::max<std::size_t>(result.queue_capacity, result.num_lines);

    return result;
}

export inline auto make_pipeline_builder_config(const PipelineConfig& config, const nlohmann::json& raw_config)
    -> PipelineBuilderConfig {
    auto result = make_pipeline_builder_config(raw_config);
    result.recipe = make_pipeline_runtime_recipe(config, raw_config, result.num_lines, result.name);
    result.dag_plan = make_runtime_dag_plan(result.recipe);
    result.num_lines = result.recipe.num_lines;
    result.name = result.recipe.pipeline_name;
    return result;
}

}
