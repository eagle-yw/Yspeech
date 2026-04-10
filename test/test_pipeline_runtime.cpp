#include "gtest/gtest.h"
#include <nlohmann/json.hpp>

import yspeech;
import std;

namespace {

struct TestStatusCapability {
    explicit TestStatusCapability(const nlohmann::json& config = {}) {
        init(config);
    }

    void init(const nlohmann::json& config) {
        status_ = config.value("status", std::string("capability"));
        const auto phase = config.value("phase", std::string("pre"));
        phase_ = phase == "post"
            ? yspeech::CapabilityPhase::Post
            : yspeech::CapabilityPhase::Pre;
    }

    void apply(yspeech::RuntimeContext& runtime) {
        if (runtime.emit_status) {
            runtime.emit_status(status_);
        }
    }

    auto phase() const -> yspeech::CapabilityPhase {
        return phase_;
    }

private:
    std::string status_ = "capability";
    yspeech::CapabilityPhase phase_ = yspeech::CapabilityPhase::Pre;
};

yspeech::CapabilityRegistrar<TestStatusCapability> test_status_capability_registrar("TestStatusCapability");

}

TEST(PipelineRuntime, RecipeInferenceFromPipelineConfig) {
    nlohmann::json config = {
        {"name", "streaming_asr"},
        {"runtime", {{"pipeline_lines", 3}, {"pipeline_name", "runtime.pipeline"}}},
        {"source", {{"type", "file"}, {"path", "audio.wav"}}},
        {"pipelines", {
            {
                {"id", "vad_stage"},
                {"max_concurrency", 1},
                {"ops", {{
                    {"id", "vad"},
                    {"name", "SileroVad"}
                }}}
            },
            {
                {"id", "feature_stage"},
                {"max_concurrency", 2},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"}
                }}}
            },
            {
                {"id", "asr_stage"},
                {"max_concurrency", 4},
                {"ops", {{
                    {"id", "asr"},
                    {"name", "AsrParaformer"}
                }}}
            }
        }}
    };

    auto pipeline_config = yspeech::PipelineConfig::from_json(config);
    auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);

    EXPECT_EQ(builder_config.num_lines, 3u);
    EXPECT_EQ(builder_config.name, "streaming_asr");
    ASSERT_EQ(builder_config.recipe.stages.size(), 4u);

    const auto* source_stage = builder_config.recipe.stage_by_role(yspeech::PipelineStageRole::Source);
    ASSERT_NE(source_stage, nullptr);
    EXPECT_EQ(source_stage->stage_id, "source_stage");
    ASSERT_EQ(source_stage->core_names.size(), 1u);
    EXPECT_EQ(source_stage->core_names.front(), "FileSource");
    EXPECT_EQ(source_stage->init_params.value("__core_id", std::string{}), "source");
    EXPECT_EQ(source_stage->init_params.value("core_name", std::string{}), "FileSource");
    EXPECT_EQ(source_stage->init_params.value("path", std::string{}), "audio.wav");

    const auto* vad_stage = builder_config.recipe.stage_by_role(yspeech::PipelineStageRole::Vad);
    ASSERT_NE(vad_stage, nullptr);
    EXPECT_EQ(vad_stage->stage_id, "vad_stage");
    EXPECT_EQ(vad_stage->max_concurrency, 1u);
    EXPECT_EQ(vad_stage->depends_on, std::vector<std::string>({"source_stage"}));
    EXPECT_EQ(vad_stage->init_params.value("__core_id", std::string{}), "vad");
    EXPECT_EQ(vad_stage->init_params.value("core_name", std::string{}), "SileroVad");

    const auto* feature_stage = builder_config.recipe.stage_by_role(yspeech::PipelineStageRole::Feature);
    ASSERT_NE(feature_stage, nullptr);
    EXPECT_EQ(feature_stage->stage_id, "feature_stage");
    EXPECT_EQ(feature_stage->max_concurrency, 2u);

    const auto* asr_stage = builder_config.recipe.stage_by_role(yspeech::PipelineStageRole::Asr);
    ASSERT_NE(asr_stage, nullptr);
    EXPECT_EQ(asr_stage->stage_id, "asr_stage");
    EXPECT_EQ(asr_stage->max_concurrency, 4u);
}

TEST(PipelineRuntime, UnknownMixedStageStaysUnknown) {
    nlohmann::json config = {
        {"source", {{"type", "stream"}}},
        {"pipelines", {
            {
                {"id", "mixed_stage"},
                {"ops", {
                    {{"id", "vad"}, {"name", "SileroVad"}},
                    {{"id", "asr"}, {"name", "AsrParaformer"}}
                }}
            }
        }}
    };

    auto pipeline_config = yspeech::PipelineConfig::from_json(config);
    auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);

    ASSERT_EQ(builder_config.recipe.stages.size(), 2u);
    const auto* source_stage = builder_config.recipe.stage_by_role(yspeech::PipelineStageRole::Source);
    ASSERT_NE(source_stage, nullptr);
    EXPECT_EQ(source_stage->core_names.front(), "StreamSource");
    const auto* mixed_stage = builder_config.recipe.stage_by_id("mixed_stage");
    ASSERT_NE(mixed_stage, nullptr);
    EXPECT_EQ(mixed_stage->role, yspeech::PipelineStageRole::Unknown);
}

TEST(PipelineRuntime, ExplicitSourceStageIsRecognizedAsSourceRole) {
    nlohmann::json config = {
        {"pipelines", {
            {
                {"id", "capture_stage"},
                {"ops", {{
                    {"id", "capture"},
                    {"name", "PassThroughSource"}
                }}}
            },
            {
                {"id", "vad_stage"},
                {"depends_on", {"capture_stage"}},
                {"ops", {{
                    {"id", "vad"},
                    {"name", "SileroVad"}
                }}}
            }
        }}
    };

    auto pipeline_config = yspeech::PipelineConfig::from_json(config);
    auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);

    ASSERT_EQ(builder_config.recipe.stages.size(), 2u);
    const auto* source_stage = builder_config.recipe.stage_by_role(yspeech::PipelineStageRole::Source);
    ASSERT_NE(source_stage, nullptr);
    EXPECT_EQ(source_stage->stage_id, "capture_stage");
    EXPECT_EQ(source_stage->core_names.front(), "PassThroughSource");
    EXPECT_EQ(source_stage->init_params.value("__core_id", std::string{}), "capture");
    EXPECT_EQ(source_stage->init_params.value("core_name", std::string{}), "PassThroughSource");
}

TEST(PipelineRuntime, InvalidJoinPolicyIsRejectedByPipelineConfig) {
    nlohmann::json config = {
        {"pipelines", {
            {
                {"id", "feature_stage"},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"}
                }}}
            },
            {
                {"id", "merge_stage"},
                {"depends_on", {"feature_stage", "other_stage"}},
                {"join_policy", "first_ready"},
                {"ops", {{
                    {"id", "merge"},
                    {"name", "JoinBarrier"}
                }}}
            }
        }}
    };

    EXPECT_THROW((void)yspeech::PipelineConfig::from_json(config), std::runtime_error);
}

TEST(PipelineRuntime, JoinPolicyOnNonJoinNodeIsRejectedByPipelineConfig) {
    nlohmann::json config = {
        {"pipelines", {
            {
                {"id", "feature_stage"},
                {"join_policy", "all_of"},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"}
                }}}
            }
        }}
    };

    EXPECT_THROW((void)yspeech::PipelineConfig::from_json(config), std::runtime_error);
}

TEST(PipelineRuntime, JoinTimeoutOnNonJoinNodeIsRejectedByPipelineConfig) {
    nlohmann::json config = {
        {"pipelines", {
            {
                {"id", "feature_stage"},
                {"join_timeout_ms", 50},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"}
                }}}
            }
        }}
    };

    EXPECT_THROW((void)yspeech::PipelineConfig::from_json(config), std::runtime_error);
}

TEST(PipelineRuntime, UnknownStageDependencyIsRejectedByPipelineConfig) {
    nlohmann::json config = {
        {"pipelines", {
            {
                {"id", "asr_stage"},
                {"depends_on", {"missing_stage"}},
                {"ops", {{
                    {"id", "asr"},
                    {"name", "AsrParaformer"}
                }}}
            }
        }}
    };

    EXPECT_THROW((void)yspeech::PipelineConfig::from_json(config), std::runtime_error);
}

TEST(PipelineRuntime, CyclicStageDependenciesAreRejectedByPipelineConfig) {
    nlohmann::json config = {
        {"pipelines", {
            {
                {"id", "stage_a"},
                {"depends_on", {"stage_b"}},
                {"ops", {{
                    {"id", "a"},
                    {"name", "SileroVad"}
                }}}
            },
            {
                {"id", "stage_b"},
                {"depends_on", {"stage_a"}},
                {"ops", {{
                    {"id", "b"},
                    {"name", "KaldiFbank"}
                }}}
            }
        }}
    };

    EXPECT_THROW((void)yspeech::PipelineConfig::from_json(config), std::runtime_error);
}

TEST(PipelineRuntime, StageDagDependenciesArePreservedInRecipe) {
    nlohmann::json config = {
        {"name", "streaming_dag"},
        {"runtime", {{"pipeline_lines", 2}}},
        {"pipelines", {
            {
                {"id", "capture_stage"},
                {"ops", {{
                    {"id", "capture"},
                    {"name", "PassThroughSource"}
                }}}
            },
            {
                {"id", "vad_stage"},
                {"depends_on", {"capture_stage"}},
                {"ops", {{
                    {"id", "vad"},
                    {"name", "SileroVad"}
                }}}
            },
            {
                {"id", "feature_stage"},
                {"depends_on", {"vad_stage"}},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"}
                }}}
            },
            {
                {"id", "speaker_stage"},
                {"depends_on", {"vad_stage"}},
                {"ops", {{
                    {"id", "speaker"},
                    {"name", "PassThroughBranch"}
                }}}
            },
            {
                {"id", "merge_stage"},
                {"depends_on", {"feature_stage", "speaker_stage"}},
                {"ops", {{
                    {"id", "merge"},
                    {"name", "JoinBarrier"}
                }}}
            }
        }}
    };

    auto pipeline_config = yspeech::PipelineConfig::from_json(config);
    auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);

    const auto* vad_stage = builder_config.recipe.stage_by_id("vad_stage");
    ASSERT_NE(vad_stage, nullptr);
    EXPECT_EQ(vad_stage->node_kind, yspeech::RuntimeNodeKind::Branch);
    EXPECT_EQ(vad_stage->depends_on, std::vector<std::string>({"capture_stage"}));

    const auto* merge_stage = builder_config.recipe.stage_by_id("merge_stage");
    ASSERT_NE(merge_stage, nullptr);
    EXPECT_EQ(merge_stage->node_kind, yspeech::RuntimeNodeKind::Join);
    EXPECT_EQ(merge_stage->join_policy, yspeech::JoinPolicy::AllOf);
    EXPECT_EQ(merge_stage->depends_on, (std::vector<std::string>{"feature_stage", "speaker_stage"}));

    const auto* feature_stage = builder_config.recipe.stage_by_id("feature_stage");
    ASSERT_NE(feature_stage, nullptr);
    EXPECT_EQ(feature_stage->node_kind, yspeech::RuntimeNodeKind::Linear);
    EXPECT_EQ(feature_stage->downstream_ids, std::vector<std::string>({"merge_stage"}));

    ASSERT_EQ(builder_config.dag_plan.root_ids, std::vector<std::string>({"capture_stage"}));
    ASSERT_EQ(builder_config.dag_plan.stage_paths.size(), 4u);
    const auto has_path = [&](const std::vector<std::string>& expected) {
        return std::ranges::find(builder_config.dag_plan.stage_paths, expected) !=
               builder_config.dag_plan.stage_paths.end();
    };
    EXPECT_TRUE(has_path({"capture_stage", "vad_stage"}));
    EXPECT_TRUE(has_path({"feature_stage"}));
    EXPECT_TRUE(has_path({"speaker_stage"}));
    EXPECT_TRUE(has_path({"merge_stage"}));

    const auto* vad_node = builder_config.dag_plan.node_by_id("vad_stage");
    ASSERT_NE(vad_node, nullptr);
    EXPECT_EQ(vad_node->stage_path_index, 0u);
    EXPECT_EQ(vad_node->position_in_path, 1u);

    const auto* merge_node = builder_config.dag_plan.node_by_id("merge_stage");
    ASSERT_NE(merge_node, nullptr);
    EXPECT_EQ(merge_node->node_kind, yspeech::RuntimeNodeKind::Join);
    EXPECT_EQ(merge_node->join_policy, yspeech::JoinPolicy::AllOf);
}

TEST(PipelineRuntime, JoinPolicyIsPreservedInRecipeAndDagPlan) {
    nlohmann::json config = {
        {"name", "streaming_join_policy"},
        {"runtime", {{"pipeline_lines", 2}}},
        {"pipelines", {
            {
                {"id", "capture_stage"},
                {"ops", {{
                    {"id", "capture"},
                    {"name", "PassThroughSource"}
                }}}
            },
            {
                {"id", "feature_stage"},
                {"depends_on", {"capture_stage"}},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"}
                }}}
            },
            {
                {"id", "speaker_stage"},
                {"depends_on", {"capture_stage"}},
                {"ops", {{
                    {"id", "speaker"},
                    {"name", "PassThroughBranch"}
                }}}
            },
            {
                {"id", "merge_stage"},
                {"depends_on", {"feature_stage", "speaker_stage"}},
                {"join_policy", "any_of"},
                {"ops", {{
                    {"id", "merge"},
                    {"name", "JoinBarrier"}
                }}}
            }
        }}
    };

    auto pipeline_config = yspeech::PipelineConfig::from_json(config);
    auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);

    const auto* merge_stage = builder_config.recipe.stage_by_id("merge_stage");
    ASSERT_NE(merge_stage, nullptr);
    EXPECT_EQ(merge_stage->node_kind, yspeech::RuntimeNodeKind::Join);
    EXPECT_EQ(merge_stage->join_policy, yspeech::JoinPolicy::AnyOf);

    const auto* merge_node = builder_config.dag_plan.node_by_id("merge_stage");
    ASSERT_NE(merge_node, nullptr);
    EXPECT_EQ(merge_node->join_policy, yspeech::JoinPolicy::AnyOf);
}

TEST(PipelineRuntime, JoinTimeoutIsPreservedInRecipeAndDagPlan) {
    nlohmann::json config = {
        {"name", "streaming_join_timeout"},
        {"runtime", {{"pipeline_lines", 2}}},
        {"pipelines", {
            {
                {"id", "feature_stage"},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"}
                }}}
            },
            {
                {"id", "speaker_stage"},
                {"ops", {{
                    {"id", "speaker"},
                    {"name", "PassThroughBranch"}
                }}}
            },
            {
                {"id", "merge_stage"},
                {"depends_on", {"feature_stage", "speaker_stage"}},
                {"join_policy", "all_of"},
                {"join_timeout_ms", 25},
                {"ops", {{
                    {"id", "merge"},
                    {"name", "JoinBarrier"}
                }}}
            }
        }}
    };

    auto pipeline_config = yspeech::PipelineConfig::from_json(config);
    auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);

    const auto* merge_stage = builder_config.recipe.stage_by_id("merge_stage");
    ASSERT_NE(merge_stage, nullptr);
    EXPECT_EQ(merge_stage->join_timeout_ms, 25);

    const auto* merge_node = builder_config.dag_plan.node_by_id("merge_stage");
    ASSERT_NE(merge_node, nullptr);
    EXPECT_EQ(merge_node->join_timeout_ms, 25);
}

TEST(PipelineRuntime, FeatureStageBuildsSegmentFeatureData) {
    yspeech::SegmentRegistry registry;
    auto segment = registry.create_segment("default", 0);
    {
        std::lock_guard lock(segment->mutex);
        segment->audio_accumulated.assign(16000, 0.1f);
        segment->end_ms = 1000;
        segment->lifecycle = yspeech::SegmentLifecycle::Closed;
    }

    yspeech::FeatureStage stage;
    nlohmann::json config = {
        {"sample_rate", 16000},
        {"num_bins", 80},
        {"frame_length_ms", 25.0},
        {"frame_shift_ms", 10.0}
    };
    stage.init(config);

    yspeech::RuntimeContext runtime;
    yspeech::PipelineToken token;
    token.stream_id = "default";
    token.segment_id = segment->segment_id;
    token.kind = yspeech::PipelineTokenKind::SegmentFinal;
    token.audio.assign(16000, 0.1f);
    token.eos = true;

    stage.process(token, runtime, registry);

    {
        std::lock_guard lock(segment->mutex);
        EXPECT_TRUE(segment->feature_ready);
        EXPECT_GT(segment->feature_version, 0u);
        EXPECT_EQ(segment->audio_samples_consumed_by_feature, 16000u);
        EXPECT_TRUE(segment->features_accumulated.empty());
    }
    {
        std::scoped_lock lock(runtime.stream_feature_mutex);
        auto it = runtime.stream_feature_snapshots.find("default");
        ASSERT_NE(it, runtime.stream_feature_snapshots.end());
        ASSERT_TRUE(it->second.chunks);
        EXPECT_FALSE(it->second.chunks->empty());
        EXPECT_GT(it->second.version, 0u);
        EXPECT_GT(it->second.feature_count, 0);
        EXPECT_GT(it->second.delta_feature_count, 0);
    }
    EXPECT_FALSE(token.feature_frames.empty());
    EXPECT_GT(token.feature_version, 0u);
}

TEST(PipelineRuntime, FeatureStageRecordsCorePhaseTimings) {
    yspeech::SegmentRegistry registry;
    auto segment = registry.create_segment("default", 0);
    {
        std::lock_guard lock(segment->mutex);
        segment->audio_accumulated.assign(16000, 0.1f);
        segment->end_ms = 1000;
        segment->lifecycle = yspeech::SegmentLifecycle::Closed;
    }

    yspeech::FeatureStage stage;
    nlohmann::json config = {
        {"__core_id", "fbank"},
        {"sample_rate", 16000},
        {"num_bins", 80},
        {"frame_length_ms", 25.0},
        {"frame_shift_ms", 10.0}
    };
    stage.init(config);

    yspeech::ProcessingStats stats;
    stage.bind_stats(&stats);

    yspeech::RuntimeContext runtime;
    yspeech::PipelineToken token;
    token.stream_id = "default";
    token.segment_id = segment->segment_id;
    token.kind = yspeech::PipelineTokenKind::SegmentFinal;
    token.audio.assign(16000, 0.1f);
    token.eos = true;

    stage.process(token, runtime, registry);
    stage.deinit();

    EXPECT_TRUE(stats.core_timings.contains("fbank"));
    EXPECT_TRUE(stats.core_phase_timings.contains("fbank:extract"));
    EXPECT_TRUE(stats.core_phase_timings.contains("fbank:lfr"));
}

TEST(PipelineRuntime, StageCapabilitiesRunAroundCoreProcessing) {
    yspeech::SegmentRegistry registry;
    yspeech::FeatureStage stage;
    nlohmann::json config = {
        {"sample_rate", 16000},
        {"num_bins", 80},
        {"frame_length_ms", 25.0},
        {"frame_shift_ms", 10.0},
        {"capabilities", {
            {
                {"name", "TestStatusCapability"},
                {"params", {
                    {"status", "pre_cap"},
                    {"phase", "pre"}
                }}
            },
            {
                {"name", "TestStatusCapability"},
                {"params", {
                    {"status", "post_cap"},
                    {"phase", "post"}
                }}
            }
        }}
    };
    stage.init(config);

    std::vector<std::string> statuses;
    yspeech::RuntimeContext runtime;
    runtime.emit_status = [&](const std::string& status) {
        statuses.push_back(status);
    };

    yspeech::PipelineToken token;
    token.stream_id = "default";
    token.audio.assign(320, 0.1f);
    token.pts_begin_ms = 0;
    token.pts_end_ms = 20;

    stage.process(token, runtime, registry);
    stage.deinit();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0], "pre_cap");
    EXPECT_EQ(statuses[1], "post_cap");
}

TEST(PipelineRuntime, BuiltinStatusCapabilityEmitsConfiguredStatus) {
    yspeech::FeatureStage stage;
    nlohmann::json config = {
        {"sample_rate", 16000},
        {"num_bins", 80},
        {"frame_length_ms", 25.0},
        {"frame_shift_ms", 10.0},
        {"capabilities", {
            {
                {"name", "StatusCapability"},
                {"params", {
                    {"status", "feature_pre"},
                    {"phase", "pre"}
                }}
            },
            {
                {"name", "StatusCapability"},
                {"params", {
                    {"status", "feature_post"},
                    {"phase", "post"}
                }}
            }
        }}
    };
    stage.init(config);

    std::vector<std::string> statuses;
    yspeech::RuntimeContext runtime;
    runtime.emit_status = [&](const std::string& status) {
        statuses.push_back(status);
    };

    yspeech::SegmentRegistry registry;
    yspeech::PipelineToken token;
    token.stream_id = "default";
    token.audio.assign(320, 0.1f);
    token.pts_begin_ms = 0;
    token.pts_end_ms = 20;

    stage.process(token, runtime, registry);
    stage.deinit();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0], "feature_pre");
    EXPECT_EQ(statuses[1], "feature_post");
}

TEST(PipelineRuntime, BuiltinAlertCapabilityEmitsConfiguredAlert) {
    yspeech::FeatureStage stage;
    nlohmann::json config = {
        {"sample_rate", 16000},
        {"num_bins", 80},
        {"frame_length_ms", 25.0},
        {"frame_shift_ms", 10.0},
        {"capabilities", {
            {
                {"name", "AlertCapability"},
                {"params", {
                    {"alert_id", "feature_alert"},
                    {"message", "feature stage warning"},
                    {"phase", "post"}
                }}
            }
        }}
    };
    stage.init(config);

    std::vector<std::pair<std::string, std::string>> alerts;
    yspeech::RuntimeContext runtime;
    runtime.emit_alert = [&](const std::string& alert_id, const std::string& message) {
        alerts.emplace_back(alert_id, message);
    };

    yspeech::SegmentRegistry registry;
    yspeech::PipelineToken token;
    token.stream_id = "default";
    token.audio.assign(320, 0.1f);
    token.pts_begin_ms = 0;
    token.pts_end_ms = 20;

    stage.process(token, runtime, registry);
    stage.deinit();

    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].first, "feature_alert");
    EXPECT_EQ(alerts[0].second, "feature stage warning");
}


TEST(PipelineRuntime, PipelineExecutorRoutesBranchOutputs) {
    nlohmann::json config = {
        {"name", "streaming_branch"},
        {"runtime", {{"pipeline_lines", 2}}},
        {"pipelines", {
            {
                {"id", "capture_stage"},
                {"ops", {{
                    {"id", "capture"},
                    {"name", "PassThroughSource"}
                }}}
            },
            {
                {"id", "vad_stage"},
                {"depends_on", {"capture_stage"}},
                {"ops", {{
                    {"id", "vad"},
                    {"name", "SileroVad"}
                }}}
            },
            {
                {"id", "feature_stage"},
                {"depends_on", {"vad_stage"}},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"}
                }}}
            },
            {
                {"id", "asr_stage"},
                {"depends_on", {"vad_stage"}},
                {"ops", {{
                    {"id", "asr"},
                    {"name", "AsrParaformer"}
                }}}
            }
        }}
    };

    auto pipeline_config = yspeech::PipelineConfig::from_json(config);
    auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);

    yspeech::RuntimeContext runtime;
    yspeech::SegmentRegistry registry;
    yspeech::PipelineExecutor executor;
    executor.configure(builder_config, runtime, registry);

    std::mutex mutex;
    std::condition_variable cv;
    int vad_calls = 0;
    int feature_calls = 0;
    int asr_calls = 0;
    int terminal_calls = 0;

    executor.set_stage_callback(yspeech::PipelineStageRole::Vad, [&](yspeech::PipelineToken& token, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        std::lock_guard lock(mutex);
        ++vad_calls;
        token.segment_id = 1;
    });
    executor.set_stage_callback(yspeech::PipelineStageRole::Feature, [&](yspeech::PipelineToken&, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        std::lock_guard lock(mutex);
        ++feature_calls;
    });
    executor.set_stage_callback(yspeech::PipelineStageRole::Asr, [&](yspeech::PipelineToken&, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        std::lock_guard lock(mutex);
        ++asr_calls;
    });
    executor.set_completion_callback([&](const yspeech::PipelineToken& token, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        std::lock_guard lock(mutex);
        ++terminal_calls;
        if (token.eos && terminal_calls >= 4) {
            cv.notify_all();
        }
    });

    executor.start();

    yspeech::PipelineToken token;
    token.token_id = 1;
    token.audio = {0.1f, 0.2f, 0.3f};
    executor.push(token);

    yspeech::PipelineToken eos;
    eos.token_id = 2;
    eos.eos = true;
    eos.kind = yspeech::PipelineTokenKind::EndOfStream;
    executor.push(eos);

    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return terminal_calls >= 4;
        }));
    }

    executor.stop();

    EXPECT_GE(vad_calls, 2);
    EXPECT_GE(feature_calls, 2);
    EXPECT_GE(asr_calls, 2);
    EXPECT_EQ(terminal_calls, 4);
}

TEST(PipelineRuntime, PipelineExecutorRoutesJoinOutputs) {
    nlohmann::json config = {
        {"name", "streaming_join"},
        {"runtime", {{"pipeline_lines", 2}}},
        {"pipelines", {
            {
                {"id", "capture_stage"},
                {"ops", {{
                    {"id", "capture"},
                    {"name", "PassThroughSource"}
                }}}
            },
            {
                {"id", "vad_stage"},
                {"depends_on", {"capture_stage"}},
                {"ops", {{
                    {"id", "vad"},
                    {"name", "SileroVad"}
                }}}
            },
            {
                {"id", "feature_stage"},
                {"depends_on", {"vad_stage"}},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"}
                }}}
            },
            {
                {"id", "speaker_stage"},
                {"depends_on", {"vad_stage"}},
                {"ops", {{
                    {"id", "speaker"},
                    {"name", "PassThroughBranch"}
                }}}
            },
            {
                {"id", "merge_stage"},
                {"depends_on", {"feature_stage", "speaker_stage"}},
                {"ops", {{
                    {"id", "merge"},
                    {"name", "JoinBarrier"}
                }}}
            },
            {
                {"id", "asr_stage"},
                {"depends_on", {"merge_stage"}},
                {"ops", {{
                    {"id", "asr"},
                    {"name", "AsrParaformer"}
                }}}
            }
        }}
    };

    auto pipeline_config = yspeech::PipelineConfig::from_json(config);
    auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);

    yspeech::RuntimeContext runtime;
    yspeech::SegmentRegistry registry;
    yspeech::PipelineExecutor executor;
    executor.configure(builder_config, runtime, registry);

    std::mutex mutex;
    std::condition_variable cv;
    int vad_calls = 0;
    int feature_calls = 0;
    int asr_calls = 0;
    std::vector<std::uint64_t> asr_feature_versions;

    executor.set_stage_callback(yspeech::PipelineStageRole::Vad, [&](yspeech::PipelineToken& token, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        std::lock_guard lock(mutex);
        ++vad_calls;
        token.segment_id = 7;
    });
    executor.set_stage_callback(yspeech::PipelineStageRole::Feature, [&](yspeech::PipelineToken& token, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        std::lock_guard lock(mutex);
        ++feature_calls;
        token.feature_version = 42;
        token.feature_frames = {{{0.1f, 0.2f, 0.3f}}};
    });
    executor.set_stage_callback(yspeech::PipelineStageRole::Asr, [&](yspeech::PipelineToken& token, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        std::lock_guard lock(mutex);
        ++asr_calls;
        asr_feature_versions.push_back(token.feature_version);
    });
    executor.set_completion_callback([&](const yspeech::PipelineToken& token, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        std::lock_guard lock(mutex);
        if (token.eos && asr_calls >= 2) {
            cv.notify_all();
        }
    });

    executor.start();

    yspeech::PipelineToken token;
    token.token_id = 1;
    token.audio = {0.1f, 0.2f, 0.3f};
    executor.push(token);

    yspeech::PipelineToken eos;
    eos.token_id = 2;
    eos.eos = true;
    eos.kind = yspeech::PipelineTokenKind::EndOfStream;
    executor.push(eos);

    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return asr_calls >= 2;
        }));
    }

    executor.stop();

    EXPECT_GE(vad_calls, 2);
    EXPECT_GE(feature_calls, 2);
    EXPECT_GE(asr_calls, 2);
    EXPECT_TRUE(std::ranges::all_of(asr_feature_versions, [](std::uint64_t version) {
        return version == 42;
    }));
}

TEST(PipelineRuntime, PipelineExecutorRoutesAnyOfJoinOutputs) {
    nlohmann::json config = {
        {"name", "streaming_join_any_of"},
        {"runtime", {{"pipeline_lines", 2}}},
        {"pipelines", {
            {
                {"id", "capture_stage"},
                {"ops", {{
                    {"id", "capture"},
                    {"name", "PassThroughSource"}
                }}}
            },
            {
                {"id", "feature_stage"},
                {"depends_on", {"capture_stage"}},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"}
                }}}
            },
            {
                {"id", "vad_stage"},
                {"depends_on", {"capture_stage"}},
                {"ops", {{
                    {"id", "vad"},
                    {"name", "SileroVad"}
                }}}
            },
            {
                {"id", "merge_stage"},
                {"depends_on", {"feature_stage", "vad_stage"}},
                {"join_policy", "any_of"},
                {"ops", {{
                    {"id", "merge"},
                    {"name", "JoinBarrier"}
                }}}
            },
            {
                {"id", "asr_stage"},
                {"depends_on", {"merge_stage"}},
                {"ops", {{
                    {"id", "asr"},
                    {"name", "AsrParaformer"}
                }}}
            }
        }}
    };

    auto pipeline_config = yspeech::PipelineConfig::from_json(config);
    auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);

    yspeech::RuntimeContext runtime;
    yspeech::SegmentRegistry registry;
    yspeech::PipelineExecutor executor;
    executor.configure(builder_config, runtime, registry);

    std::mutex mutex;
    std::condition_variable cv;
    int asr_calls = 0;
    int vad_calls = 0;

    executor.set_stage_callback(yspeech::PipelineStageRole::Feature, [&](yspeech::PipelineToken& token, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        token.feature_version = 99;
    });
    executor.set_stage_callback(yspeech::PipelineStageRole::Vad, [&](yspeech::PipelineToken& token, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        std::lock_guard lock(mutex);
        ++vad_calls;
        token.segment_id = 123;
    });
    executor.set_stage_callback(yspeech::PipelineStageRole::Asr, [&](yspeech::PipelineToken& token, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        std::lock_guard lock(mutex);
        ++asr_calls;
        if (token.feature_version == 99) {
            cv.notify_all();
        }
    });

    executor.start();

    yspeech::PipelineToken token;
    token.token_id = 1;
    token.audio = {0.1f, 0.2f};
    executor.push(token);

    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return asr_calls >= 1;
        }));
    }

    executor.stop();

    EXPECT_GE(vad_calls, 1);
    EXPECT_GE(asr_calls, 1);
}

TEST(PipelineRuntime, PipelineExecutorRoutesTimedOutAllOfJoinOutputs) {
    nlohmann::json config = {
        {"name", "streaming_join_timeout_runtime"},
        {"runtime", {{"pipeline_lines", 2}}},
        {"pipelines", {
            {
                {"id", "capture_stage"},
                {"ops", {{
                    {"id", "capture"},
                    {"name", "PassThroughSource"}
                }}}
            },
            {
                {"id", "feature_stage"},
                {"depends_on", {"capture_stage"}},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"}
                }}}
            },
            {
                {"id", "vad_stage"},
                {"depends_on", {"capture_stage"}},
                {"ops", {{
                    {"id", "vad"},
                    {"name", "SileroVad"}
                }}}
            },
            {
                {"id", "merge_stage"},
                {"depends_on", {"feature_stage", "vad_stage"}},
                {"join_policy", "all_of"},
                {"join_timeout_ms", 0},
                {"ops", {{
                    {"id", "merge"},
                    {"name", "JoinBarrier"}
                }}}
            },
            {
                {"id", "asr_stage"},
                {"depends_on", {"merge_stage"}},
                {"ops", {{
                    {"id", "asr"},
                    {"name", "AsrParaformer"}
                }}}
            }
        }}
    };

    auto pipeline_config = yspeech::PipelineConfig::from_json(config);
    auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);

    yspeech::RuntimeContext runtime;
    yspeech::SegmentRegistry registry;
    yspeech::PipelineExecutor executor;
    executor.configure(builder_config, runtime, registry);

    std::mutex mutex;
    std::condition_variable cv;
    int asr_calls = 0;

    executor.set_stage_callback(yspeech::PipelineStageRole::Feature, [&](yspeech::PipelineToken& token, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        token.feature_version = 7;
    });
    executor.set_stage_callback(yspeech::PipelineStageRole::Vad, [&](yspeech::PipelineToken&, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        // Intentionally no-op to let timeout path prove it can continue without complete join input.
    });
    executor.set_stage_callback(yspeech::PipelineStageRole::Asr, [&](yspeech::PipelineToken& token, yspeech::RuntimeContext&, yspeech::SegmentRegistry&) {
        std::lock_guard lock(mutex);
        if (token.feature_version == 7) {
            ++asr_calls;
            cv.notify_all();
        }
    });

    executor.start();

    yspeech::PipelineToken token;
    token.token_id = 1;
    token.audio = {0.1f, 0.2f};
    executor.push(token);

    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return asr_calls >= 1;
        }));
    }

    executor.stop();
    EXPECT_GE(asr_calls, 1);
}

TEST(PipelineRuntime, PipelineExecutorRejectsConfigureWhileRunning) {
    nlohmann::json config = {
        {"name", "streaming_linear"},
        {"runtime", {{"pipeline_lines", 1}}},
        {"source", {{"type", "stream"}}},
        {"pipelines", {{
            {"id", "source_stage"},
            {"ops", {{{"id", "source"}, {"name", "StreamSource"}}}}
        }}}
    };

    auto pipeline_config = yspeech::PipelineConfig::from_json(config);
    auto builder_config = yspeech::make_pipeline_builder_config(pipeline_config, config);

    yspeech::RuntimeContext runtime;
    yspeech::SegmentRegistry registry;
    yspeech::PipelineExecutor executor;
    executor.configure(builder_config, runtime, registry);
    executor.start();

    EXPECT_THROW(executor.configure(builder_config, runtime, registry), std::runtime_error);

    executor.stop();
}
