module;

export module yspeech.runtime.runtime_dag_executor;

import std;
import yspeech.log;
import yspeech.runtime.pipeline_builder;
import yspeech.runtime.pipeline_executor;
import yspeech.runtime.pipeline_recipe;
import yspeech.runtime.runtime_context;
import yspeech.runtime.runtime_dag;
import yspeech.runtime.segment_registry;
import yspeech.runtime.token;

namespace yspeech {

export class RuntimeDagExecutor {
public:
    using StageCallback = PipelineExecutor::StageCallback;
    using TerminalCallback = PipelineExecutor::CompletionCallback;

    void configure(PipelineBuilderConfig config, RuntimeContext& runtime, SegmentRegistry& registry) {
        config_ = std::move(config);
        runtime_ = &runtime;
        registry_ = &registry;

        path_runtimes_.clear();
        stage_to_path_.clear();

        for (std::size_t index = 0; index < config_.dag_plan.stage_paths.size(); ++index) {
            const auto& stage_path = config_.dag_plan.stage_paths[index];
            if (stage_path.empty()) {
                continue;
            }

            StagePathRuntime runtime_path;
            runtime_path.path_index = index;
            runtime_path.stage_ids = stage_path;
            runtime_path.entry_stage_id = stage_path.front();
            runtime_path.exit_stage_id = stage_path.back();
            runtime_path.executor = std::make_unique<PipelineExecutor>();

            PipelineBuilderConfig sub_config;
            sub_config.num_lines = config_.num_lines;
            sub_config.name = std::format("{}.segment.{}", config_.name, index);
            sub_config.recipe.pipeline_name = sub_config.name;
            sub_config.recipe.num_lines = config_.recipe.num_lines;
            for (const auto& stage_id : runtime_path.stage_ids) {
                if (const auto* stage = config_.recipe.stage_by_id(stage_id)) {
                    sub_config.recipe.stages.push_back(*stage);
                }
                stage_to_path_[stage_id] = index;
            }
            sub_config.dag_plan.name = sub_config.name;
            sub_config.dag_plan.root_ids = {runtime_path.entry_stage_id};
            sub_config.dag_plan.stage_paths = {runtime_path.stage_ids};
            for (const auto& stage_id : runtime_path.stage_ids) {
                if (const auto* node = config_.dag_plan.node_by_id(stage_id)) {
                    sub_config.dag_plan.nodes.push_back(*node);
                }
            }

            runtime_path.executor->configure(sub_config, *runtime_, *registry_);
            runtime_path.executor->set_completion_callback(
                [this, index](const PipelineToken& token, RuntimeContext& runtime_ctx, SegmentRegistry& registry_ref) {
                    on_stage_path_output(index, token, runtime_ctx, registry_ref);
                });

            path_runtimes_.push_back(std::move(runtime_path));
        }

        rebind_callbacks();
    }

    void set_stage_callback(PipelineStageRole role, StageCallback callback) {
        stage_callbacks_[role] = std::move(callback);
        rebind_callbacks();
    }

    void set_terminal_callback(TerminalCallback callback) {
        terminal_callback_ = std::move(callback);
    }

    void start() {
        for (auto& path : path_runtimes_) {
            path.executor->start();
        }
    }

    void push(PipelineToken token) {
        if (path_runtimes_.empty()) {
            return;
        }
        if (config_.dag_plan.root_ids.empty()) {
            path_runtimes_.front().executor->push(std::move(token));
            return;
        }

        bool first = true;
        for (const auto& root_id : config_.dag_plan.root_ids) {
            auto it = stage_to_path_.find(root_id);
            if (it == stage_to_path_.end()) {
                continue;
            }
            if (first) {
                path_runtimes_[it->second].executor->push(std::move(token));
                first = false;
            } else {
                path_runtimes_[it->second].executor->push(token);
            }
        }
    }

    void finish() {
        if (config_.dag_plan.root_ids.empty()) {
            for (auto& path : path_runtimes_) {
                path.executor->finish();
            }
            return;
        }

        for (const auto& root_id : config_.dag_plan.root_ids) {
            if (auto it = stage_to_path_.find(root_id); it != stage_to_path_.end()) {
                path_runtimes_[it->second].executor->finish();
            }
        }
    }

    void stop() {
        {
            std::lock_guard lock(join_mutex_);
            join_accumulators_.clear();
        }
        for (auto& path : path_runtimes_) {
            path.executor->stop();
        }
    }

    void wait() {
        for (auto& path : path_runtimes_) {
            path.executor->wait();
        }
    }

private:
    struct StagePathRuntime {
        std::size_t path_index = 0;
        std::vector<std::string> stage_ids;
        std::string entry_stage_id;
        std::string exit_stage_id;
        std::unique_ptr<PipelineExecutor> executor;
    };

    struct JoinAccumulator {
        std::string join_stage_id;
        std::unordered_map<std::string, PipelineToken> contributions;
        bool emitted_any_of = false;
        std::chrono::steady_clock::time_point first_seen{};
    };

    void rebind_callbacks() {
        for (auto& path : path_runtimes_) {
            for (const auto& [role, callback] : stage_callbacks_) {
                path.executor->set_stage_callback(role, callback);
            }
        }
    }

    void on_stage_path_output(
        std::size_t path_index,
        const PipelineToken& token,
        RuntimeContext& runtime,
        SegmentRegistry& registry
    ) {
        if (path_index >= path_runtimes_.size()) {
            return;
        }

        const auto& exit_stage_id = path_runtimes_[path_index].exit_stage_id;
        const auto* exit_node = config_.dag_plan.node_by_id(exit_stage_id);
        if (!exit_node) {
            return;
        }

        if (exit_node->downstream_ids.empty()) {
            if (terminal_callback_) {
                terminal_callback_(token, runtime, registry);
            }
        } else {
            for (const auto& downstream_id : exit_node->downstream_ids) {
                route_to_downstream(exit_stage_id, downstream_id, token);
            }
        }

        if (token.eos) {
            path_runtimes_[path_index].executor->finish();
        }
    }

    static auto join_key_for(const PipelineToken& token) -> std::string {
        if (token.token_id != 0) {
            return std::format("token:{}", token.token_id);
        }
        if (token.segment_id.has_value()) {
            return std::format("segment:{}", *token.segment_id);
        }
        return "anonymous";
    }

    static void merge_token_into(PipelineToken& base, const PipelineToken& extra) {
        base.eos = base.eos || extra.eos;
        base.pts_begin_ms = std::min(base.pts_begin_ms, extra.pts_begin_ms);
        base.pts_end_ms = std::max(base.pts_end_ms, extra.pts_end_ms);

        if (base.audio.empty() && !extra.audio.empty()) {
            base.audio = extra.audio;
        }
        if (!base.segment_id.has_value() && extra.segment_id.has_value()) {
            base.segment_id = extra.segment_id;
        }
        if (!base.vad_segment.has_value() && extra.vad_segment.has_value()) {
            base.vad_segment = extra.vad_segment;
        }
        if (!base.asr_result.has_value() && extra.asr_result.has_value()) {
            base.asr_result = extra.asr_result;
        }
        if (base.feature_frames.empty() && !extra.feature_frames.empty()) {
            base.feature_frames = extra.feature_frames;
        }
        base.feature_version = std::max(base.feature_version, extra.feature_version);
        base.partial_version = std::max(base.partial_version, extra.partial_version);
    }

    void route_to_downstream(
        const std::string& upstream_stage_id,
        const std::string& downstream_id,
        const PipelineToken& token
    ) {
        const auto* downstream_node = config_.dag_plan.node_by_id(downstream_id);
        if (!downstream_node) {
            return;
        }

        if (downstream_node->node_kind == RuntimeNodeKind::Join) {
            auto merged = record_join_contribution(downstream_id, upstream_stage_id, token);
            if (!merged.has_value()) {
                return;
            }
            dispatch_to_path(downstream_id, *merged);
            if (merged->eos) {
                if (auto it = stage_to_path_.find(downstream_id); it != stage_to_path_.end()) {
                    path_runtimes_[it->second].executor->finish();
                }
            }
            return;
        }

        dispatch_to_path(downstream_id, token);
        if (token.eos) {
            if (auto it = stage_to_path_.find(downstream_id); it != stage_to_path_.end()) {
                path_runtimes_[it->second].executor->finish();
            }
        }
    }

    void dispatch_to_path(const std::string& stage_id, const PipelineToken& token) {
        if (auto it = stage_to_path_.find(stage_id); it != stage_to_path_.end()) {
            path_runtimes_[it->second].executor->push(token);
        }
    }

    auto record_join_contribution(
        const std::string& join_stage_id,
        const std::string& upstream_stage_id,
        const PipelineToken& token
    ) -> std::optional<PipelineToken> {
        const auto* join_node = config_.dag_plan.node_by_id(join_stage_id);
        if (!join_node) {
            return std::nullopt;
        }

        const auto join_key = std::format("{}|{}", join_stage_id, join_key_for(token));
        std::lock_guard lock(join_mutex_);
        auto& accumulator = join_accumulators_[join_key];
        accumulator.join_stage_id = join_stage_id;
        if (accumulator.first_seen == std::chrono::steady_clock::time_point{}) {
            accumulator.first_seen = std::chrono::steady_clock::now();
        }
        accumulator.contributions[upstream_stage_id] = token;

        if (join_node->join_policy == JoinPolicy::AnyOf) {
            if (accumulator.emitted_any_of && !token.eos) {
                return std::nullopt;
            }

            accumulator.emitted_any_of = true;
            auto merged = token;
            if (token.eos) {
                for (const auto& [_, contribution] : accumulator.contributions) {
                    merge_token_into(merged, contribution);
                }
                join_accumulators_.erase(join_key);
            }
            return merged;
        }

        if (accumulator.contributions.size() < join_node->depends_on.size()) {
            if (join_node->join_timeout_ms >= 0) {
                const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - accumulator.first_seen).count();
                if (elapsed_ms > join_node->join_timeout_ms) {
                    std::optional<PipelineToken> merged_timeout;
                    for (const auto& [_, contribution] : accumulator.contributions) {
                        if (!merged_timeout.has_value()) {
                            merged_timeout = contribution;
                        } else {
                            merge_token_into(*merged_timeout, contribution);
                        }
                    }
                    join_accumulators_.erase(join_key);
                    return merged_timeout;
                }
            }
            return std::nullopt;
        }

        std::optional<PipelineToken> merged;
        for (const auto& dependency_id : join_node->depends_on) {
            auto it = accumulator.contributions.find(dependency_id);
            if (it == accumulator.contributions.end()) {
                return std::nullopt;
            }
            if (!merged.has_value()) {
                merged = it->second;
            } else {
                merge_token_into(*merged, it->second);
            }
        }

        join_accumulators_.erase(join_key);
        return merged;
    }

    PipelineBuilderConfig config_;
    RuntimeContext* runtime_ = nullptr;
    SegmentRegistry* registry_ = nullptr;
    std::vector<StagePathRuntime> path_runtimes_;
    std::unordered_map<std::string, std::size_t> stage_to_path_;
    std::unordered_map<PipelineStageRole, StageCallback> stage_callbacks_;
    TerminalCallback terminal_callback_;
    std::mutex join_mutex_;
    std::unordered_map<std::string, JoinAccumulator> join_accumulators_;
};

}
