#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

import std;
import yspeech;

namespace fs = std::filesystem;

namespace {

bool paraformer_assets_exist() {
    std::ifstream model("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx");
    std::ifstream tokens("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt");
    return model.good() && tokens.good();
}

bool streaming_config_exists() {
    return fs::exists("examples/configs/streaming_paraformer_asr.json");
}

bool offline_config_exists() {
    return fs::exists("examples/configs/offline_paraformer_asr.json");
}

fs::path sample_audio_path() {
    fs::path path("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/0.wav");
    if (fs::exists(path)) {
        return path;
    }
    return {};
}

fs::path longer_audio_path() {
    fs::path path("model/asr/sherpa-onnx-paraformer-zh-2023-09-14/test_wavs/6-zh-en.wav");
    if (fs::exists(path)) {
        return path;
    }
    return sample_audio_path();
}

auto make_streaming_config(const std::string& audio_path) -> nlohmann::json {
    return nlohmann::json{
        {"name", "streaming_paraformer"},
        {"task", "asr"},
        {"mode", "streaming"},
        {"runtime", {
            {"pipeline_lines", 2}
        }},
        {"frame", {
            {"sample_rate", 16000},
            {"channels", 1},
            {"dur_ms", 10}
        }},
        {"stream", {
            {"ring_capacity_frames", 4096}
        }},
        {"source", {
            {"type", "file"},
            {"path", audio_path},
            {"playback_rate", 0.0}
        }},
        {"pipelines", {
            {
                {"id", "vad_stage"},
                {"ops", {{
                    {"id", "vad"},
                    {"name", "SileroVad"},
                    {"params", {
                        {"model_path", "model/vad/silero_vad.onnx"},
                        {"sample_rate", 16000},
                        {"min_speech_duration_ms", 100},
                        {"min_silence_duration_ms", 50}
                    }}
                }}}
            },
            {
                {"id", "feature_stage"},
                {"ops", {{
                    {"id", "fbank"},
                    {"name", "KaldiFbank"},
                    {"params", {
                        {"sample_rate", 16000},
                        {"num_bins", 80},
                        {"lfr_window_size", 7},
                        {"lfr_window_shift", 6}
                    }}
                }}}
            },
            {
                {"id", "asr_stage"},
                {"ops", {{
                    {"id", "asr"},
                    {"name", "AsrParaformer"},
                    {"params", {
                        {"model_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx"},
                        {"tokens_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt"},
                        {"language", "zh"},
                        {"min_new_feature_frames", 8}
                    }}
                }}}
            }
        }}
    };
}

auto make_streaming_branching_config(const std::string& audio_path) -> nlohmann::json {
    auto config = make_streaming_config(audio_path);
    config["name"] = "streaming_paraformer_dag_branch";
    config["pipelines"] = nlohmann::json::array({
        {
            {"id", "capture_stage"},
            {"ops", {{
                {"id", "capture"},
                {"name", "UnknownSource"}
            }}}
        },
        {
            {"id", "vad_stage"},
            {"depends_on", {"capture_stage"}},
            {"ops", {{
                {"id", "vad"},
                {"name", "SileroVad"},
                {"params", {
                    {"model_path", "model/vad/silero_vad.onnx"},
                    {"sample_rate", 16000},
                    {"min_speech_duration_ms", 100},
                    {"min_silence_duration_ms", 50}
                }}
            }}}
        },
        {
            {"id", "feature_stage"},
            {"depends_on", {"vad_stage"}},
            {"ops", {{
                {"id", "fbank"},
                {"name", "KaldiFbank"},
                {"params", {
                    {"sample_rate", 16000},
                    {"num_bins", 80},
                    {"lfr_window_size", 7},
                    {"lfr_window_shift", 6}
                }}
            }}}
        },
        {
            {"id", "speaker_stage"},
            {"depends_on", {"vad_stage"}},
            {"ops", {{
                {"id", "speaker"},
                {"name", "UnknownSpeaker"}
            }}}
        },
        {
            {"id", "asr_stage"},
            {"depends_on", {"feature_stage"}},
            {"ops", {{
                {"id", "asr"},
                {"name", "AsrParaformer"},
                {"params", {
                    {"model_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx"},
                    {"tokens_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt"},
                    {"language", "zh"},
                    {"min_new_feature_frames", 8}
                }}
            }}}
        }
    });
    return config;
}

auto make_streaming_joining_config(const std::string& audio_path) -> nlohmann::json {
    auto config = make_streaming_config(audio_path);
    config["name"] = "streaming_paraformer_dag_join";
    config["pipelines"] = nlohmann::json::array({
        {
            {"id", "capture_stage"},
            {"ops", {{
                {"id", "capture"},
                {"name", "UnknownSource"}
            }}}
        },
        {
            {"id", "vad_stage"},
            {"depends_on", {"capture_stage"}},
            {"ops", {{
                {"id", "vad"},
                {"name", "SileroVad"},
                {"params", {
                    {"model_path", "model/vad/silero_vad.onnx"},
                    {"sample_rate", 16000},
                    {"min_speech_duration_ms", 100},
                    {"min_silence_duration_ms", 50}
                }}
            }}}
        },
        {
            {"id", "feature_stage"},
            {"depends_on", {"vad_stage"}},
            {"ops", {{
                {"id", "fbank"},
                {"name", "KaldiFbank"},
                {"params", {
                    {"sample_rate", 16000},
                    {"num_bins", 80},
                    {"lfr_window_size", 7},
                    {"lfr_window_shift", 6}
                }}
            }}}
        },
        {
            {"id", "merge_stage"},
            {"depends_on", {"vad_stage", "feature_stage"}},
            {"join_policy", "all_of"},
            {"ops", {{
                {"id", "merge"},
                {"name", "UnknownMerge"}
            }}}
        },
        {
            {"id", "asr_stage"},
            {"depends_on", {"merge_stage"}},
            {"ops", {{
                {"id", "asr"},
                {"name", "AsrParaformer"},
                {"params", {
                    {"model_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/model.int8.onnx"},
                    {"tokens_path", "model/asr/sherpa-onnx-paraformer-zh-2023-09-14/tokens.txt"},
                    {"language", "zh"},
                    {"min_new_feature_frames", 8}
                }}
            }}}
        }
    });
    return config;
}

auto make_streaming_join_timeout_config(const std::string& audio_path) -> nlohmann::json {
    auto config = make_streaming_joining_config(audio_path);
    config["name"] = "streaming_paraformer_dag_join_timeout";
    for (auto& stage : config["pipelines"]) {
        if (stage.contains("id") && stage["id"] == "merge_stage") {
            stage["join_timeout_ms"] = 0;
        }
    }
    return config;
}

void expect_streaming_pipeline_has_asr_events(const fs::path& audio_path) {
    yspeech::Engine engine(make_streaming_config(audio_path.string()));
    std::vector<yspeech::EngineEvent> events;
    engine.on_event([&](const yspeech::EngineEvent& event) {
        events.push_back(event);
    });

    engine.start();
    engine.finish();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!engine.input_eof_reached() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    engine.stop();

    EXPECT_TRUE(engine.input_eof_reached());
    EXPECT_FALSE(events.empty());
    const auto has_asr_event = std::ranges::any_of(events, [](const yspeech::EngineEvent& event) {
        return event.kind == yspeech::EngineEventKind::ResultPartial ||
               event.kind == yspeech::EngineEventKind::ResultSegmentFinal ||
               event.kind == yspeech::EngineEventKind::ResultStreamFinal;
    });
    EXPECT_TRUE(has_asr_event);
}

void expect_streaming_branch_pipeline_has_asr_events(const fs::path& audio_path) {
    yspeech::Engine engine(make_streaming_branching_config(audio_path.string()));
    std::vector<yspeech::EngineEvent> events;
    engine.on_event([&](const yspeech::EngineEvent& event) {
        events.push_back(event);
    });

    engine.start();
    engine.finish();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!engine.input_eof_reached() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    engine.stop();

    EXPECT_TRUE(engine.input_eof_reached());
    EXPECT_FALSE(events.empty());
    const auto has_asr_event = std::ranges::any_of(events, [](const yspeech::EngineEvent& event) {
        return event.kind == yspeech::EngineEventKind::ResultPartial ||
               event.kind == yspeech::EngineEventKind::ResultSegmentFinal ||
               event.kind == yspeech::EngineEventKind::ResultStreamFinal;
    });
    EXPECT_TRUE(has_asr_event);
}

void expect_streaming_join_pipeline_has_asr_events(const fs::path& audio_path) {
    yspeech::Engine engine(make_streaming_joining_config(audio_path.string()));
    std::vector<yspeech::EngineEvent> events;
    engine.on_event([&](const yspeech::EngineEvent& event) {
        events.push_back(event);
    });

    engine.start();
    engine.finish();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!engine.input_eof_reached() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    engine.stop();

    EXPECT_TRUE(engine.input_eof_reached());
    EXPECT_FALSE(events.empty());
    const auto has_asr_event = std::ranges::any_of(events, [](const yspeech::EngineEvent& event) {
        return event.kind == yspeech::EngineEventKind::ResultPartial ||
               event.kind == yspeech::EngineEventKind::ResultSegmentFinal ||
               event.kind == yspeech::EngineEventKind::ResultStreamFinal;
    });
    EXPECT_TRUE(has_asr_event);
}

void expect_streaming_join_timeout_pipeline_has_asr_events(const fs::path& audio_path) {
    yspeech::Engine engine(make_streaming_join_timeout_config(audio_path.string()));
    std::vector<yspeech::EngineEvent> events;
    engine.on_event([&](const yspeech::EngineEvent& event) {
        events.push_back(event);
    });

    engine.start();
    engine.finish();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!engine.input_eof_reached() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    engine.stop();

    EXPECT_TRUE(engine.input_eof_reached());
    EXPECT_FALSE(events.empty());
    const auto has_asr_event = std::ranges::any_of(events, [](const yspeech::EngineEvent& event) {
        return event.kind == yspeech::EngineEventKind::ResultPartial ||
               event.kind == yspeech::EngineEventKind::ResultSegmentFinal ||
               event.kind == yspeech::EngineEventKind::ResultStreamFinal;
    });
    EXPECT_TRUE(has_asr_event);
}

}

TEST(TestAsrRealAudio, OfflineEngineSmoke) {
    const auto audio_path = sample_audio_path();
    if (audio_path.empty() || !offline_config_exists() || !paraformer_assets_exist()) {
        GTEST_SKIP() << "Offline demo assets not found";
    }

    yspeech::EngineConfigOptions options;
    options.audio_path = audio_path.string();
    options.playback_rate = 1.0;
    yspeech::Engine engine(std::string("examples/configs/offline_paraformer_asr.json"), options);

    std::vector<yspeech::EngineEvent> events;
    engine.on_event([&](const yspeech::EngineEvent& event) { events.push_back(event); });

    engine.start();
    engine.finish();
    while (!engine.input_eof_reached()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    engine.stop();

    EXPECT_FALSE(events.empty());
}

TEST(TestAsrRealAudio, StreamingEngineSmoke) {
    const auto audio_path = sample_audio_path();
    if (audio_path.empty() || !streaming_config_exists() || !paraformer_assets_exist()) {
        GTEST_SKIP() << "Streaming demo assets not found";
    }

    yspeech::EngineConfigOptions options;
    options.audio_path = audio_path.string();
    options.playback_rate = 0.0;
    yspeech::Engine engine(std::string("examples/configs/streaming_paraformer_asr.json"), options);

    std::vector<yspeech::EngineEvent> events;
    engine.on_event([&](const yspeech::EngineEvent& event) { events.push_back(event); });

    engine.start();
    engine.finish();
    while (!engine.input_eof_reached()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    engine.stop();

    EXPECT_FALSE(events.empty());
}

TEST(TestAsrRealAudio, ExplicitFrameSourceOverridesConfigSource) {
    const auto audio_path = sample_audio_path();
    if (audio_path.empty() || !streaming_config_exists() || !paraformer_assets_exist()) {
        GTEST_SKIP() << "Streaming demo assets not found";
    }

    yspeech::EngineConfigOptions options;
    options.audio_path = audio_path.string();
    options.playback_rate = 0.01;
    yspeech::Engine engine(std::string("examples/configs/streaming_paraformer_asr.json"), options);

    auto override_source = std::make_shared<yspeech::MicSource>("override");
    override_source->push_frame(override_source->make_frame({}, 0, 0, true, false, 16000, 1));
    auto pipeline_source = std::make_shared<yspeech::AudioFramePipelineSource>(override_source);
    engine.set_frame_source(pipeline_source);

    engine.start();
    engine.finish();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (!engine.input_eof_reached() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    engine.stop();

    EXPECT_TRUE(engine.input_eof_reached());
}

TEST(TestAsrRealAudio, StreamingEnginePipelineSmoke) {
    const auto audio_path = sample_audio_path();
    if (audio_path.empty() || !paraformer_assets_exist()) {
        GTEST_SKIP() << "Streaming assets not found";
    }

    expect_streaming_pipeline_has_asr_events(audio_path);
}

TEST(TestAsrRealAudio, StreamingEnginePipelineConfigFileSmoke) {
    const auto audio_path = sample_audio_path();
    if (audio_path.empty() || !paraformer_assets_exist() || !streaming_config_exists()) {
        GTEST_SKIP() << "Streaming config-file assets not found";
    }

    yspeech::EngineConfigOptions options;
    options.audio_path = audio_path.string();
    options.playback_rate = 0.0;
    yspeech::Engine engine(std::string("examples/configs/streaming_paraformer_asr.json"), options);

    std::vector<yspeech::EngineEvent> events;
    engine.on_event([&](const yspeech::EngineEvent& event) { events.push_back(event); });

    engine.start();
    engine.finish();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!engine.input_eof_reached() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    engine.stop();

    EXPECT_TRUE(engine.input_eof_reached());
    EXPECT_FALSE(events.empty());
    const auto has_asr_event = std::ranges::any_of(events, [](const yspeech::EngineEvent& event) {
        return event.kind == yspeech::EngineEventKind::ResultPartial ||
               event.kind == yspeech::EngineEventKind::ResultSegmentFinal ||
               event.kind == yspeech::EngineEventKind::ResultStreamFinal;
    });
    EXPECT_TRUE(has_asr_event);
}

TEST(TestAsrRealAudio, StreamingEnginePipelineLongerAudioSmoke) {
    const auto audio_path = longer_audio_path();
    if (audio_path.empty() || !paraformer_assets_exist()) {
        GTEST_SKIP() << "Streaming taskflow long-audio assets not found";
    }

    expect_streaming_pipeline_has_asr_events(audio_path);
}

TEST(TestAsrRealAudio, StreamingEngineBranchingPipelineSmoke) {
    const auto audio_path = sample_audio_path();
    if (audio_path.empty() || !paraformer_assets_exist()) {
        GTEST_SKIP() << "Streaming branching assets not found";
    }

    expect_streaming_branch_pipeline_has_asr_events(audio_path);
}

TEST(TestAsrRealAudio, StreamingEngineJoiningPipelineSmoke) {
    const auto audio_path = sample_audio_path();
    if (audio_path.empty() || !paraformer_assets_exist()) {
        GTEST_SKIP() << "Streaming joining assets not found";
    }

    expect_streaming_join_pipeline_has_asr_events(audio_path);
}

TEST(TestAsrRealAudio, StreamingEngineJoinTimeoutPipelineSmoke) {
    const auto audio_path = sample_audio_path();
    if (audio_path.empty() || !paraformer_assets_exist()) {
        GTEST_SKIP() << "Streaming join-timeout assets not found";
    }

    expect_streaming_join_timeout_pipeline_has_asr_events(audio_path);
}
